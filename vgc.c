#define vgc_c

#include <string.h>

#include "vs.h"

#include "vdebug.h"
#include "vdo.h"
#include "vfunc.h"
#include "vgc.h"
#include "vmem.h"
#include "vobject.h"
#include "vstate.h"
#include "vstring.h"
#include "vtable.h"


/*
** internal state for collector while inside the atomic phase. The
** collector should never be in this state while running regular code.
*/
#define GCSinsideatomic		(GCSpause + 1)

/*
** cost of sweeping one element (the size of a small object divided
** by some adjust for the sweep speed)
*/
// 释放一个对象的开销
#define GCSWEEPCOST	((sizeof(TString) + 4) / 4)

/* maximum number of elements to sweep in each single step */
// 一次最多清扫的元素的个数
#define GCSWEEPMAX	(cast_int((GCSTEPSIZE / GCSWEEPCOST) / 4))

/* cost of calling one finalizer */
#define GCFINALIZECOST	GCSWEEPCOST


/*
** macro to adjust 'stepmul': 'stepmul' is actually used like
** 'stepmul / STEPMULADJ' (value chosen by tests)
*/
#define STEPMULADJ		200


/*
** macro to adjust 'pause': 'pause' is actually used like
** 'pause / PAUSEADJ' (value chosen by tests)
*/
#define PAUSEADJ		100


/*
** 'makewhite' erases all color bits then sets only the current white
** bit
*/
// 颜色位为0,其他位为1
#define maskcolors	(~(bitmask(BLACKBIT) | WHITEBITS))
// 清除x的所有颜色,然后设置x的颜色为当前白
#define makewhite(g,x)	\
 (x->marked = cast_byte((x->marked & maskcolors) | vsC_white(g)))

// 设置white0和white1的两个标记位都为0
#define white2gray(x)	resetbits(x->marked, WHITEBITS)
// 设置black的标记位为0
#define black2gray(x)	resetbit(x->marked, BLACKBIT)


#define valiswhite(x)   (iscollectable(x) && iswhite(gcvalue(x)))

// 只有value为nil时,key才会被设置为VS_TDEADKEY
// 这个宏用来确保不存在key已经设置为VS_TDEADKEY了但是value不为nil
#define checkdeadkey(n)	vs_assert(!ttisdeadkey(gkey(n)) || ttisnil(gval(n)))


#define checkconsistency(obj)  \
  vs_longassert(!iscollectable(obj) || righttt(obj))


// markvalue用于标记TValue
// markobject用于标记各种GCObject
// 只有白对象才会调用reallymarkobject进行实际的标记,所以重复调用markvalue,markobject是没有问题的
#define markvalue(g,o) { checkconsistency(o); \
  if (valiswhite(o)) reallymarkobject(g,gcvalue(o)); }

#define markobject(g,t)	{ if (iswhite(t)) reallymarkobject(g, obj2gco(t)); }

/*
** mark an object that can be NULL (either because it is really optional,
** or it was stripped as debug info, or inside an uncompleted structure)
*/
// markobjectN和markobject功能相同,但是可以传入NULL
#define markobjectN(g,t)	{ if (t) markobject(g,t); }

static void reallymarkobject (global_State *g, GCObject *o);


/*
** {======================================================
** Generic functions
** =======================================================
*/


/*
** one after last element in a hash array
*/
// 获取表哈希部分最后一个的下一个
#define gnodelast(h)	gnode(h, cast(size_t, sizenode(h)))


/*
** link collectable object 'o' into list pointed by 'p'
*/
// 将o加入到p链表中
#define linkgclist(o,p)	((o)->gclist = (p), (p) = obj2gco(o))


/*
** If key is not marked, mark its entry as dead. This allows key to be
** collected, but keeps its entry in the table.  A dead node is needed
** when VS looks up for a key (it may be part of a chain) and when
** traversing a weak table (key might be removed from the table during
** traversal). Other places never manipulate dead keys, because its
** associated nil value is enough to signal that the entry is logically
** empty.
*/
// Node对应的value必须是nil 如果key是白色,那么设置key为VS_TDEADKEY类型
static void removeentry (Node *n) {
  vs_assert(ttisnil(gval(n)));
  // key是白色状态,设置key为VS_TDEADKEY类型
  if (valiswhite(gkey(n)))
    setdeadvalue(wgkey(n));  /* unused and unmarked key; remove it */
}


// barrier和barrierback都需要区分是否在清扫阶段
// 在清扫阶段之前,标记还没结束,可以设置父子对象颜色让标记阶段重新扫描
// 如果已经进入清扫阶段,就直接设置父对象为新白,放弃这一轮的清扫,等待下一次gc周期处理
/*
** barrier that moves collector forward, that is, mark the white object
** being pointed by a black object. (If in sweep phase, clear the black
** object to white [sweep it] to avoid other barrier calls for this
** same object.)
*/
// 清扫阶段之前:标记子对象为灰
// 清扫阶段:标记父对象为新白
void vsC_barrier_ (vs_State *L, GCObject *o, GCObject *v) {
  global_State *g = G(L);
  vs_assert(isblack(o) && iswhite(v) && !isdead(g, v) && !isdead(g, o));
  // 传播或原子阶段标记v
  if (keepinvariant(g))  /* must keep invariant? */
    reallymarkobject(g, v);  /* restore invariant */
  // 清扫阶段已经从旧白变成了新白
  // 设置父对象o为新白,避免在清扫阶段被清扫,等到下一轮再处理
  // ??
  else {  /* sweep phase */
    vs_assert(issweepphase(g));
    makewhite(g, o);  /* mark main obj. as white to avoid other barriers */
  }
}


/*
** barrier that moves collector backward, that is, mark the black object
** pointing to a white object as gray again.
*/
// 调用barrierback的父对象一定是表对象
// 该函数直接将父对象的表设置为灰色
void vsC_barrierback_ (vs_State *L, Table *t) {
  global_State *g = G(L);
  vs_assert(isblack(t) && !isdead(g, t));
  black2gray(t);  /* make table gray (again) */
  linkgclist(t, g->grayagain);
}


/*
** barrier for assignments to closed upvalues. Because upvalues are
** shared among closures, it is impossible to know the color of all
** closures pointing to it. So, we assume that the object being assigned
** must be marked.
*/
void vsC_upvalbarrier_ (vs_State *L, UpVal *uv) {
  global_State *g = G(L);
  GCObject *o = gcvalue(uv->v);
  vs_assert(!upisopen(uv));  /* ensured by macro vsC_upvalbarrier */
  if (keepinvariant(g))
    markobject(g, o);
}


void vsC_fix (vs_State *L, GCObject *o) {
  global_State *g = G(L);
  vs_assert(g->allgc == o);  /* object must be 1st in 'allgc' list! */
  white2gray(o);  /* they will be gray forever */
  g->allgc = o->next;  /* remove object from 'allgc' list */
  o->next = g->fixedgc;  /* link it to 'fixedgc' list */
  g->fixedgc = o;
}


/*
** create a new collectable object (with given type and size) and link
** it to 'allgc' list.
*/
// 所有gc对象都由该函数创建,初始化CommonHeader并将对象加入allgc链表
GCObject *vsC_newobj (vs_State *L, int tt, size_t sz) {
  global_State *g = G(L);
  // 传入novariant(tt)没有意义,只是为了可读性
  GCObject *o = cast(GCObject *, vsM_newobject(L, novariant(tt), sz));
  // 初始化CommonHeader的三个字段
  o->marked = vsC_white(g);
  o->tt = tt;
  o->next = g->allgc;
  // 将新建的对象加入到allgc链表
  g->allgc = o;
  return o;
}

/* }====================================================== */



/*
** {======================================================
** Mark functions
** =======================================================
*/


/*
** mark an object. Userdata, strings, and closed upvalues are visited
** and turned black here. Other objects are marked gray and added
** to appropriate list to be visited (and turned black) later. (Open
** upvalues are already linked in 'headuv' list.)
*/
// 传入的所有对象应该都是白对象
// 字符串:最终变成黑色
// 用户数据:标记元表,标记关联的用户值,变成黑色
// 线程,函数,函数原型和表:变成灰色,加入到g->gray中
static void reallymarkobject (global_State *g, GCObject *o) {
 reentry:
  // 设置white0和white1的两个标记位都为0
  // 如果传入的o不是black的话,执行后就变成gray了
  white2gray(o);
  switch (o->tt) {
    // 短字符串和长字符串都转换成black
    // 让g->GCmemtrav加上字符串对象占用的空间
    case VS_TSHRSTR: {
      gray2black(o);
      g->GCmemtrav += sizelstring(gco2ts(o)->shrlen);
      break;
    }
    case VS_TLNGSTR: {
      gray2black(o);
      g->GCmemtrav += sizelstring(gco2ts(o)->u.lnglen);
      break;
    }
    case VS_TUSERDATA: {
      TValue uvalue;
      // 标记userdata的元表
      gray2black(o);
      g->GCmemtrav += sizeudata(gco2u(o));
      // 读取userdata结果写入uvalue
      getuservalue(g->mainthread, gco2u(o), &uvalue);
      // 标记关联的用户值
      if (valiswhite(&uvalue)) {  /* markvalue(g, &uvalue); */
        o = gcvalue(&uvalue);
        goto reentry;
      }
      break;
    }
    // 以下类型都是将对象加入到gray链表中
    case VS_TLCL: {
      linkgclist(gco2lcl(o), g->gray);
      break;
    }
    case VS_TCCL: {
      linkgclist(gco2ccl(o), g->gray);
      break;
    }
    case VS_TTABLE: {
      linkgclist(gco2t(o), g->gray);
      break;
    }
    case VS_TTHREAD: {
      linkgclist(gco2th(o), g->gray);
      break;
    }
    case VS_TPROTO: {
      linkgclist(gco2p(o), g->gray);
      break;
    }
    default: vs_assert(0); break;
  }
}


/*
** Mark all values stored in marked open upvalues from non-marked threads.
** (Values from marked threads were already marked when traversing the
** thread.) Remove from the list threads that no longer have upvalues and
** not-marked threads.
*/
// 将白线程或者没有upvalue的线程从twups链表中移除
// 标记白线程中的所有在traverseLclosure中被访问到的upvalue
static void remarkupvals (global_State *g) {
  vs_State *thread;
  vs_State **p = &g->twups;
  // 遍历twups链表
  while ((thread = *p) != NULL) {
    // 经过propagatemark函数,线程类型还是灰色
    vs_assert(!isblack(thread));  /* threads are never black */
    // 灰的线程而且有upvalue 直接跳过
    if (isgray(thread) && thread->openupval != NULL)
      p = &thread->twups;  /* keep marked thread with upvalues in the list */
    // 没被标记或者没有upvalue
    else {  /* thread is not marked or without upvalues */
      UpVal *uv;
      // 从链表中移除线程
      *p = thread->twups;  /* remove thread from the list */
      // 在isintwups中就是通过这个判断一个线程是不是在twups链表中的
      thread->twups = thread;  /* mark that it is out of list */
      for (uv = thread->openupval; uv != NULL; uv = uv->u.open.next) {
        if (uv->u.open.touched) {
          markvalue(g, uv->v);  /* remark upvalue's value */
          uv->u.open.touched = 0;
        }
      }
    }
  }
}


/*
** mark root set and reset all gray lists, to start a new collection
*/
// 清空灰色,标记根节点集合
static void restartcollection (global_State *g) {
  // 清空灰色相关链表
  g->gray = g->grayagain = NULL;
  // 标记主线程
  markobject(g, g->mainthread);
  // 标记注册表
  markvalue(g, &g->l_registry);
}

/* }====================================================== */


/*
** {======================================================
** Traverse functions
** =======================================================
*/

// 遍历标记表
// 数组部分直接标记
// 哈希部分如果value为nil标记key为dead,否则标记key和value
static lu_mem traversetable (global_State *g, Table *h) {
  // limit作为for循环的结束标志,limit是哈希部分的最后一个元素的下一个
  Node *n, *limit = gnodelast(h);
  unsigned int i;
  // 标记数组部分
  for (i = 0; i < h->sizearray; i++)  /* traverse array part */
    markvalue(g, &h->array[i]);
  // 标记哈希部分
  for (n = gnode(h, 0); n < limit; n++) {  /* traverse hash part */
    // 确保key如果是VS_TDEADKEY那么value一定是nil
    checkdeadkey(n);
    // 如果value是nil 标记key为死亡
    if (ttisnil(gval(n)))  /* entry is empty? */
      removeentry(n);  /* remove it */
    else {
      // key,value都不是nil 标记key和value
      vs_assert(!ttisnil(gkey(n)));
      markvalue(g, gkey(n));  /* mark key */
      markvalue(g, gval(n));  /* mark value */
    }
  }
  // 表占用的空间包括Table对象+TValue*数组部分大小+Node*哈希部分大小
  return sizeof(Table) + sizeof(TValue) * h->sizearray +
                         sizeof(Node) * cast(size_t, allocsizenode(h));
}


/*
** Traverse a prototype. (While a prototype is being build, its
** arrays can be larger than needed; the extra slots are filled with
** NULL, so the use of 'markobjectN')
*/
static int traverseproto (global_State *g, Proto *f) {
  int i;
  if (f->cache && iswhite(f->cache))
    f->cache = NULL;  /* allow cache to be collected */
  markobjectN(g, f->source);
  for (i = 0; i < f->sizek; i++)  /* mark literals */
    markvalue(g, &f->k[i]);
  for (i = 0; i < f->sizeupvalues; i++)  /* mark upvalue names */
    markobjectN(g, f->upvalues[i].name);
  for (i = 0; i < f->sizep; i++)  /* mark nested protos */
    markobjectN(g, f->p[i]);
  for (i = 0; i < f->sizelocvars; i++)  /* mark local-variable names */
    markobjectN(g, f->locvars[i].varname);
  return sizeof(Proto) + sizeof(Instruction) * f->sizecode +
                         sizeof(Proto *) * f->sizep +
                         sizeof(TValue) * f->sizek +
                         sizeof(int) * f->sizelineinfo +
                         sizeof(LocVar) * f->sizelocvars +
                         sizeof(Upvaldesc) * f->sizeupvalues;
}


static lu_mem traverseCclosure (global_State *g, CClosure *cl) {
  int i;
  for (i = 0; i < cl->nupvalues; i++)  /* mark its upvalues */
    markvalue(g, &cl->upvalue[i]);
  return sizeCclosure(cl->nupvalues);
}

/*
** open upvalues point to values in a thread, so those values should
** be marked when the thread is traversed except in the atomic phase
** (because then the value cannot be changed by the thread and the
** thread may not be traversed again)
*/
static lu_mem traverseLclosure (global_State *g, LClosure *cl) {
  int i;
  // 标记vs闭包的原型
  markobjectN(g, cl->p);  /* mark its prototype */
  // 标记闭包保存的所有的upvalue
  for (i = 0; i < cl->nupvalues; i++) {  /* mark its upvalues */
    UpVal *uv = cl->upvals[i];
    if (uv != NULL) {
      // 开放的upvalue到remarkupvals函数中一次性标记
      if (upisopen(uv) && g->gcstate != GCSinsideatomic)
        uv->u.open.touched = 1;  /* can be marked in 'remarkupvals' */
      else
        markvalue(g, uv->v);
    }
  }
  return sizeLclosure(cl->nupvalues);
}


static lu_mem traversethread (global_State *g, vs_State *th) {
  StkId o = th->stack;
  if (o == NULL)
    return 1;  /* stack not completely built yet */
  vs_assert(g->gcstate == GCSinsideatomic ||
             th->openupval == NULL || isintwups(th));
  // 遍历标记栈上元素
  for (; o < th->top; o++)  /* mark live elements in the stack */
    markvalue(g, o);
  if (g->gcstate == GCSinsideatomic) {  /* final traversal? */
    StkId lim = th->stack + th->stacksize;  /* real end of stack */
    for (; o < lim; o++)  /* clear not-marked stack slice */
      setnilvalue(o);
    /* 'remarkupvals' may have removed thread from 'twups' list */
    if (!isintwups(th) && th->openupval != NULL) {
      // 将当前线程加入到twups链表中
      th->twups = g->twups;  /* link it back to the list */
      g->twups = th;
    }
  }
  else if (g->gckind != KGC_EMERGENCY)
    vsD_shrinkstack(th); /* do not change stack in emergency cycle */
  return (sizeof(vs_State) + sizeof(TValue) * th->stacksize +
          sizeof(CallInfo) * th->nci);
}


/*
** traverse one gray object, turning it to black (except for threads,
** which are always gray).
*/
// GCSpropagate阶段的核心函数
// 原理是从g->gray链表上取出一个元素标记为黑色,然后遍历所有子元素标记为灰色
// 表,vs函数,c函数,Proto对象都是变成黑色
// 线程类型还是灰色,并且移入grayagain链表中
static void propagatemark (global_State *g) {
  lu_mem size;
  GCObject *o = g->gray;
  vs_assert(isgray(o));
  // 开始遍历标记灰对象,所以灰对象要变黑,子对象变灰
  gray2black(o);
  switch (o->tt) {
    case VS_TTABLE: {
      Table *h = gco2t(o);
      g->gray = h->gclist;  /* remove from 'gray' list */
      size = traversetable(g, h);
      break;
    }
    case VS_TLCL: {
      LClosure *cl = gco2lcl(o);
      g->gray = cl->gclist;  /* remove from 'gray' list */
      size = traverseLclosure(g, cl);
      break;
    }
    case VS_TCCL: {
      CClosure *cl = gco2ccl(o);
      g->gray = cl->gclist;  /* remove from 'gray' list */
      size = traverseCclosure(g, cl);
      break;
    }
    // 线程类型很特殊,没有变成黑色,还是灰色,需要使用grayagain
    case VS_TTHREAD: {
      vs_State *th = gco2th(o);
      g->gray = th->gclist;  /* remove from 'gray' list */
      linkgclist(th, g->grayagain);  /* insert into 'grayagain' list */
      black2gray(o);
      size = traversethread(g, th);
      break;
    }
    case VS_TPROTO: {
      Proto *p = gco2p(o);
      g->gray = p->gclist;  /* remove from 'gray' list */
      size = traverseproto(g, p);
      break;
    }
    default: vs_assert(0); return;
  }
  g->GCmemtrav += size;
}


static void propagateall (global_State *g) {
  while (g->gray) propagatemark(g);
}

/* }====================================================== */


/*
** {======================================================
** Sweep Functions
** =======================================================
*/


// 让对应的upvalue的引用计数减1
// 减1后如果为0且是关闭的upvalue,释放该upvalue
void vsC_upvdeccount (vs_State *L, UpVal *uv) {
  vs_assert(uv->refcount > 0);
  uv->refcount--;
  // 引用计数为0且是关闭的upvalue
  if (uv->refcount == 0 && !upisopen(uv))
    vsM_free(L, uv);
}


// 释放一个vs闭包
// 遍历所有upvalue,让引用计数减1,如果为0释放upvalue
// 然后释放闭包自己
static void freeLclosure (vs_State *L, LClosure *cl) {
  int i;
  for (i = 0; i < cl->nupvalues; i++) {
    UpVal *uv = cl->upvals[i];
    if (uv)
      vsC_upvdeccount(L, uv);
  }
  // LClosure对象后面存着upvalue的指针
  // sizeof(LClosure)只包括一个upvalue指针,所以需要根据upvalue的个数计算释放空间
  vsM_freemem(L, cl, sizeLclosure(cl->nupvalues));
}


// 释放传入GCObject类型对象
static void freeobj (vs_State *L, GCObject *o) {
  switch (o->tt) {
    // 原型对象需要释放几个保存的数组以及对象自身
    case VS_TPROTO: vsF_freeproto(L, gco2p(o)); break;
    // vs闭包需要释放保存的upvalue和对象自身
    case VS_TLCL: {
      freeLclosure(L, gco2lcl(o));
      break;
    }
    // c闭包直接释放对象占用的空间
    case VS_TCCL: {
      vsM_freemem(L, o, sizeCclosure(gco2ccl(o)->nupvalues));
      break;
    }
    case VS_TTABLE: vsH_free(L, gco2t(o)); break;
    // 释放CallInfo链表,vs栈以及线程对象自身
    case VS_TTHREAD: vsE_freethread(L, gco2th(o)); break;
    case VS_TUSERDATA: vsM_freemem(L, o, sizeudata(gco2u(o))); break;
    // 释放短字符串需要从stringtable中先删除短字符串
    case VS_TSHRSTR:
      vsS_remove(L, gco2ts(o));  /* remove it from hash table */
      vsM_freemem(L, o, sizelstring(gco2ts(o)->shrlen));
      break;
    case VS_TLNGSTR: {
      vsM_freemem(L, o, sizelstring(gco2ts(o)->u.lnglen));
      break;
    }
    default: vs_assert(0);
  }
}


#define sweepwholelist(L,p)	sweeplist(L,p,MAX_LUMEM)
static GCObject **sweeplist (vs_State *L, GCObject **p, lu_mem count);


/*
** sweep at most 'count' elements from a list of GCObjects erasing dead
** objects, where a dead object is one marked with the old (non current)
** white; change all non-dead objects back to white, preparing for next
** collection cycle. Return where to continue the traversal or NULL if
** list is finished.
*/
// 从p链表上删除其他白对象,并将这些对象的空间释放
// 将没有被删除的对象全部设置为当前白
// 利用count控制遍历链表的个数
// 返回值代表下次继续遍历的开始位置,如果是NULL说明已经遍历结束
static GCObject **sweeplist (vs_State *L, GCObject **p, lu_mem count) {
  global_State *g = G(L);
  int ow = otherwhite(g);
  int white = vsC_white(g);  /* current white */
  while (*p != NULL && count-- > 0) {
    GCObject *curr = *p;
    int marked = curr->marked;
    // 当前对象的颜色是其他白,就被释放
    if (isdeadm(ow, marked)) {  /* is 'curr' dead? */
      // 从链表上移除即将被释放的对象
      *p = curr->next;  /* remove 'curr' from list */
      freeobj(L, curr);  /* erase 'curr' */
    }
    // 颜色不是其他白的对象,颜色都被修改为当前白
    else {  /* change mark to 'white' */
      // 设置curr的颜色为当前白
      curr->marked = cast_byte((marked & maskcolors) | white);
      p = &curr->next;  /* go to next element */
    }
  }
  return (*p == NULL) ? NULL : p;
}

/* }====================================================== */

/*
** If possible, shrink string table
*/
static void checkSizes (vs_State *L, global_State *g) {
  if (g->gckind != KGC_EMERGENCY) {
    l_mem olddebt = g->GCdebt;
    if (g->strt.nuse < g->strt.size / 4)  /* string table too big? */
      vsS_resize(L, g->strt.size / 2);  /* shrink it a little */
    g->GCestimate += g->GCdebt - olddebt;  /* update estimate */
  }
}


/* }====================================================== */



/*
** {======================================================
** GC control
** =======================================================
*/


/*
** Set a reasonable "time" to wait before starting a new GC cycle; cycle
** will start when memory use hits threshold. (Division by 'estimate'
** should be OK: it cannot be zero (because VS cannot even start with
** less than PAUSEADJ bytes).
*/
static void setpause (global_State *g) {
  // 不考虑越界情况,代码这样写
  // l_mem threshold = g->gcpause * g->GCestimate / PAUSEADJ;
  // vsE_setdebt(g, gettotalbytes(g) - threshold);
  // gcpause默认是200,PAUSEADJ默认是100
  // 也就是threshold默认是 2*g->GCestimate
  // 在vsC_fullgc代码中可以确认当GC循环执行到GCScallfin状态以前,g->GCestimate与gettotalbytes(g)必然相等
  // 也就是说设置的g->GCdebt = -g->GCestimate
  // 需要分配g->GCestimate大小的内存后,再启动垃圾回收
  l_mem threshold, debt;
  l_mem estimate = g->GCestimate / PAUSEADJ;  /* adjust 'estimate' */
  vs_assert(estimate > 0);
  threshold = (g->gcpause < MAX_LMEM / estimate)  /* overflow? */
            ? estimate * g->gcpause  /* no overflow */
            : MAX_LMEM;  /* overflow; truncate to maximum */
  debt = gettotalbytes(g) - threshold;
  vsE_setdebt(g, debt);
}


/*
** Enter first sweep phase.
** The call to 'sweeplist' tries to make pointer point to an object
** inside the list (instead of to the header), so that the real sweep do
** not need to skip objects created between "now" and the start of the
** real sweep.
*/
// 设置gcstate为GCSswpallgc
// 尝试设置sweepgc为allgc中下一项
static void entersweep (vs_State *L) {
  global_State *g = G(L);
  g->gcstate = GCSswpallgc;
  vs_assert(g->sweepgc == NULL);
  // atomic在进入GCSswpallgc前会将global_State.sweepgc指向global_State.allgc上几乎所有对象
  // 之所以用’几乎’这个词, 是因为它是通过函数`sweeplist`来做转换的
  // 这里不直接使用g->sweepgc = &g->allgc,是希望g->sweepgc尽可能为&(g->allgc->next)的值
  // 这样做是原因是,在进入GCSswpallgc状态后,整个GC又进入了增量模式
  // 此时可能会有很多新创建的对象(而这些对象是下一轮GC的白色值,因为必然不会被回收)挂在global_State.allgc上
  // g->sweepgc指向&(g->allgc->next)可以尽可能的避免这些额外干扰.
  g->sweepgc = sweeplist(L, &g->allgc, 1);
}


void vsC_freeallobjects (vs_State *L) {
  global_State *g = G(L);
  g->currentwhite = WHITEBITS; /* this "white" makes all objects look dead */
  g->gckind = KGC_NORMAL;
  sweepwholelist(L, &g->allgc);
  sweepwholelist(L, &g->fixedgc);  /* collect fixed objects */
  vs_assert(g->strt.nuse == 0);
}


static l_mem atomic (vs_State *L) {
  global_State *g = G(L);
  l_mem work;
  GCObject *grayagain = g->grayagain;  /* save original list */
  // 线程对象应该是灰对象
  vs_assert(!iswhite(g->mainthread));
  g->gcstate = GCSinsideatomic;
  g->GCmemtrav = 0;  /* start counting work */
  markobject(g, L);  /* mark running thread */
  /* registry and global metatables may be changed by API */
  markvalue(g, &g->l_registry);
  /* remark occasional upvalues of (maybe) dead threads */
  remarkupvals(g);
  propagateall(g);  /* propagate changes */
  work = g->GCmemtrav;  /* stop counting (do not recount 'grayagain') */
  g->gray = grayagain;
  propagateall(g);  /* traverse 'grayagain' list */
  g->GCmemtrav = 0;  /* restart counting */
  vsS_clearcache(g);
  // 切换当前白,之后创建的对象的颜色都是新白
  g->currentwhite = cast_byte(otherwhite(g));  /* flip current white */
  work += g->GCmemtrav;  /* complete counting */
  return work;  /* estimate of memory marked by 'atomic' */
}


// 清理sweepgc链表上的元素,必要时更新gc的状态
// 如果清理完毕,设置gc的状态为传入的状态,并更新sweepgc为nextlist
// 没清理完成,达到了清理的上限,就不更新gc状态
static lu_mem sweepstep (vs_State *L, global_State *g,
                         int nextstate, GCObject **nextlist) {
  if (g->sweepgc) {
    l_mem olddebt = g->GCdebt;
    g->sweepgc = sweeplist(L, g->sweepgc, GCSWEEPMAX);
    g->GCestimate += g->GCdebt - olddebt;  /* update estimate */
    // 如果本次清理达到了GCSWEEPMAX,导致没有清理完
    // 那么就不更新gc的状态
    if (g->sweepgc)  /* is there still something to sweep? */
      return (GCSWEEPMAX * GCSWEEPCOST);
  }
  /* else enter next state */
  // 成功清理了sweepgc链表,进入下一个状态
  g->gcstate = nextstate;
  g->sweepgc = nextlist;
  return 0;
}


static lu_mem singlestep (vs_State *L) {
  global_State *g = G(L);
  switch (g->gcstate) {
    case GCSpause: {
      g->GCmemtrav = g->strt.size * sizeof(GCObject*);
      restartcollection(g);
      g->gcstate = GCSpropagate;
      return g->GCmemtrav;
    }
    case GCSpropagate: {
      g->GCmemtrav = 0;
      vs_assert(g->gray);
      propagatemark(g);
      // 如果gray链表被清空了,就进入下一个状态GCSatomic
       if (g->gray == NULL)  /* no more gray objects? */
        g->gcstate = GCSatomic;  /* finish propagate phase */
      return g->GCmemtrav;  /* memory traversed in this step */
    }
    // 原子阶段即将结束前会切换当前白,也就是设置为新白
    // 所以清扫阶段只会清理旧白对象,也就是其他白
    case GCSatomic: {
      lu_mem work;
      // 进入该状态前要清空gray链表
      // 因为GCSpropagate和GCSatomic状态之间也可能生成了新的对象
      // 新对象通过vsC_barrier加入到了gray链表
      propagateall(g);  /* make sure gray list is empty */
      work = atomic(L);  /* work is what was traversed by 'atomic' */
      entersweep(L);
      g->GCestimate = gettotalbytes(g);  /* first estimate */;
      return work;
    }
    // 将sweepgc链表上的所有死对象释放,也就是其他白对象
    case GCSswpallgc: {  /* sweep "regular" objects */
      return sweepstep(L, g, GCSswpend, NULL);
    }
    // 设置主线程是当前白
    case GCSswpend: {  /* finish sweeps */
      makewhite(g, g->mainthread);  /* sweep main thread */
      checkSizes(L, g);
      g->gcstate = GCSpause;
      return 0;
    }
    default: vs_assert(0); return 0;
  }
}


/*
** advances the garbage collector until it reaches a state allowed
** by 'statemask'
*/
void vsC_runtilstate (vs_State *L, int statesmask) {
  global_State *g = G(L);
  while (!testbit(statesmask, g->gcstate))
    singlestep(L);
}


/*
** get GC debt and convert it from Kb to 'work units' (avoid zero debt
** and overflows)
*/
// 该函数就是让GCdebt乘以一个倍率stepmul
static l_mem getdebt (global_State *g) {
  l_mem debt = g->GCdebt;
  // g->gcstepmul默认是200
  int stepmul = g->gcstepmul;
  if (debt <= 0) return 0;  /* minimal debt */
  else {
    debt = (debt / STEPMULADJ) + 1;
    debt = (debt < MAX_LMEM / stepmul) ? debt * stepmul : MAX_LMEM;
    return debt;
  }
}

/*
** performs a basic GC step when collector is running
*/
void vsC_step (vs_State *L) {
  global_State *g = G(L);
  // 根据倍率stepmul计算出来当前的debt
  l_mem debt = getdebt(g);  /* GC deficit (be paid now) */
  if (!g->gcrunning) {  /* not running? */
    vsE_setdebt(g, -GCSTEPSIZE * 10);  /* avoid being called too often */
    return;
  }
  do {  /* repeat until pause or enough "credit" (negative debt) */
    lu_mem work = singlestep(L);  /* perform one single step */
    debt -= work;
  } while (debt > -GCSTEPSIZE && g->gcstate != GCSpause);
  // 如果这一轮垃圾回收结束了,设置g->GCdebt = -g->GCestimate
  // 也就是再分配g->GCestimate大小的内存后,再次开始垃圾回收
  if (g->gcstate == GCSpause)
    setpause(g);  /* pause until next cycle */
  else {
    // 转换为Kb单位,设置g->GCdebt
    debt = (debt / g->gcstepmul) * STEPMULADJ;  /* convert 'work units' to Kb */
    vsE_setdebt(g, debt);
  }
}


/*
** Performs a full GC cycle; if 'isemergency', set a flag to avoid
** some operations which could change the interpreter state in some
** unexpected ways (running finalizers and shrinking some structures).
** Before running the collection, check 'keepinvariant'; if it is true,
** there may be some objects marked as black, so the collector has
** to sweep all objects to turn them back to white (as white has not
** changed, nothing will be collected).
*/
void vsC_fullgc (vs_State *L, int isemergency) {
  global_State *g = G(L);
  vs_assert(g->gckind == KGC_NORMAL);
  if (isemergency) g->gckind = KGC_EMERGENCY;  /* set flag */
  if (keepinvariant(g)) {  /* black objects? */
    entersweep(L); /* sweep everything to turn them back to white */
  }
  /* finish any pending sweep phase to start a new cycle */
  vsC_runtilstate(L, bitmask(GCSpause));
  vsC_runtilstate(L, ~bitmask(GCSpause));  /* start new collection */
  vsC_runtilstate(L, bitmask(GCSswpend));  /* run up to finalizers */
  /* estimate must be correct after a full GC cycle */
  vs_assert(g->GCestimate == gettotalbytes(g));
  vsC_runtilstate(L, bitmask(GCSpause));  /* finish collection */
  g->gckind = KGC_NORMAL;
  setpause(g);
}

/* }====================================================== */


