#define vstate_c


#include <stddef.h>
#include <string.h>

#include "vs.h"

#include "vapi.h"
#include "vdebug.h"
#include "vdo.h"
#include "vfunc.h"
#include "vgc.h"
#include "vlex.h"
#include "vmem.h"
#include "vstate.h"
#include "vstring.h"
#include "vtable.h"


#define VSI_GCPAUSE	200  /* 200% */

#define VSI_GCMUL	200 /* GC runs 'twice the speed' of memory allocation */


/*
** a macro to help the creation of a unique random seed when a state is
** created; the seed is used to randomize hashes.
*/
#include <time.h>
#define vsi_makeseed()		cast(unsigned int, 13134447)



/*
** thread state + extra space
*/
typedef struct LX {
  lu_byte extra_[VS_EXTRASPACE];
  vs_State l;
} LX;


/*
** Main thread combines a thread state and the global state
*/
typedef struct LG {
  LX l;
  global_State g;
} LG;



#define fromstate(L)	(cast(LX *, cast(lu_byte *, (L)) - offsetof(LX, l)))


/*
** Compute an initial seed as random as possible. Rely on Address Space
** Layout Randomization (if present) to increase randomness..
*/
#define addbuff(b,p,e) \
  { size_t t = cast(size_t, e); \
    memcpy(b + p, &t, sizeof(t)); p += sizeof(t); }

static unsigned int makeseed (vs_State *L) {
  char buff[4 * sizeof(size_t)];
  unsigned int h = vsi_makeseed();
  int p = 0;
  addbuff(buff, p, L);  /* heap variable */
  addbuff(buff, p, &h);  /* local variable */
  addbuff(buff, p, vsO_nilobject);  /* global variable */
  addbuff(buff, p, &vs_newstate);  /* public function */
  vs_assert(p == sizeof(buff));
  return vsS_hash(buff, p, h);
}


/*
** set GCdebt to a new value keeping the value (totalbytes + GCdebt)
** invariant (and avoiding underflows in 'totalbytes')
*/
// 设置GCdebt的只为debt,并且保证totalbytes+新GCdebt的值也保持不变
void vsE_setdebt (global_State *g, l_mem debt) {
  l_mem tb = gettotalbytes(g);
  vs_assert(tb > 0);
  if (debt < tb - MAX_LMEM)
    debt = tb - MAX_LMEM;  /* will make 'totalbytes == MAX_LMEM' */
  // 保证totalbytes+GCdebt结果不变
  g->totalbytes = tb - debt;
  g->GCdebt = debt;
}


CallInfo *vsE_extendCI (vs_State *L) {
  CallInfo *ci = vsM_new(L, CallInfo);
  vs_assert(L->ci->next == NULL);
  L->ci->next = ci;
  ci->previous = L->ci;
  ci->next = NULL;
  L->nci++;
  return ci;
}


/*
** free all CallInfo structures not in use by a thread
*/
// 释放当前的L->ci链表,这个链表有一个空的头节点不要被释放
// 并根据释放的CallInfo个数设置nci的值
void vsE_freeCI (vs_State *L) {
  CallInfo *ci = L->ci;
  CallInfo *next = ci->next;
  ci->next = NULL;
  // 这里跳过头节点
  while ((ci = next) != NULL) {
    next = ci->next;
    vsM_free(L, ci);
    L->nci--;
  }
}


/*
** free half of the CallInfo structures not in use by a thread
*/
void vsE_shrinkCI (vs_State *L) {
  CallInfo *ci = L->ci;
  CallInfo *next2;  /* next's next */
  /* while there are two nexts */
  while (ci->next != NULL && (next2 = ci->next->next) != NULL) {
    vsM_free(L, ci->next);  /* free next */
    L->nci--;
    ci->next = next2;  /* remove 'next' from the list */
    next2->previous = ci;
    ci = next2;  /* keep next's next */
  }
}


// 初始化栈空间为BASIC_STACK_SIZE
// 初始化第一个CallInfo
// 执行后栈内存放一个nil值,ci->func指向这个nil
static void stack_init (vs_State *L1, vs_State *L) {
  int i; CallInfo *ci;
  /* initialize stack array */
  L1->stack = vsM_newvector(L, BASIC_STACK_SIZE, TValue);
  L1->stacksize = BASIC_STACK_SIZE;
  for (i = 0; i < BASIC_STACK_SIZE; i++)
    setnilvalue(L1->stack + i);  /* erase new stack */
  L1->top = L1->stack;
  // stack_last标记的并不是真正的最后的空间,最后还剩余EXTRA_STACK的空间备用
  L1->stack_last = L1->stack + L1->stacksize - EXTRA_STACK;
  /* initialize first ci */
  // 初始化base_ci
  ci = &L1->base_ci;
  ci->next = ci->previous = NULL;
  ci->callstatus = 0;
  ci->func = L1->top;
  setnilvalue(L1->top++);  /* 'function' entry for this 'ci' */
  ci->top = L1->top + VS_MINSTACK;
  L1->ci = ci;
}


// 释放CallInfo链表以及vs栈
static void freestack (vs_State *L) {
  if (L->stack == NULL)
    return;  /* stack not completely built yet */
  // 因为释放整个CallInfo链表,先设置L->ci为base_ci,使得从头开始释放
  L->ci = &L->base_ci;  /* free the entire 'ci' list */
  vsE_freeCI(L);
  // 从头开始释放后nci应该为0了
  vs_assert(L->nci == 0);
  vsM_freearray(L, L->stack, L->stacksize);  /* free stack array */
}


/*
** Create registry table and its predefined values
*/
// 初始化全局注册表
// 初始化后就是一个数组部分为2的表 包括主线程和一个全局表(初始化为空表)
static void init_registry (vs_State *L, global_State *g) {
  TValue temp;
  /* create registry */
  Table *registry = vsH_new(L);
  sethvalue(L, &g->l_registry, registry);
  // 该表的数组部分长度是2
  vsH_resize(L, registry, VS_RIDX_LAST, 0);
  /* registry[VS_RIDX_MAINTHREAD] = L */
  // 把这个注册表的数组部分的第一个元素赋值为主线程的状态机L
  setthvalue(L, &temp, L);  /* temp = L */
  vsH_setint(L, registry, VS_RIDX_MAINTHREAD, &temp);
  /* registry[VS_RIDX_GLOBALS] = table of globals */
  // 把注册表的数组部分的第二个元素赋值为全局表, 全局表现在就是一个新建的表
  sethvalue(L, &temp, vsH_new(L));  /* temp = new table (global table) */
  vsH_setint(L, registry, VS_RIDX_GLOBALS, &temp);
}


/*
** open parts of the state that may cause memory-allocation errors.
** ('g->version' != NULL flags that the state was completely build)
*/
// 初始化栈,第一个CallInfo,全局注册表
static void f_vsopen (vs_State *L, void *ud) {
  global_State *g = G(L);
  UNUSED(ud);
  stack_init(L, L);  /* init stack */
  init_registry(L, g);
  vsS_init(L);
  vsX_init(L);
  // vs_newstate中最初设置gcrunning为0,这里修改为1
  g->gcrunning = 1;  /* allow gc */
  g->version = vs_version(NULL);
}


/*
** preinitialize a thread with consistent values without allocating
** any memory (to avoid errors)
*/
static void preinit_thread (vs_State *L, global_State *g) {
  G(L) = g;
  L->stack = NULL;
  L->ci = NULL;
  L->nci = 0;
  L->stacksize = 0;
  L->twups = L;  /* thread has no upvalues */
  L->errorJmp = NULL;
  L->nCcalls = 0;
  L->openupval = NULL;
  L->status = VS_OK;
  L->errfunc = 0;
}


static void close_state (vs_State *L) {
  global_State *g = G(L);
  vsF_close(L, L->stack);  /* close all upvalues for this thread */
  vsC_freeallobjects(L);  /* collect all objects */
  vsM_freearray(L, G(L)->strt.hash, G(L)->strt.size);
  freestack(L);
  vs_assert(gettotalbytes(g) == sizeof(LG));
  (*g->frealloc)(g->ud, fromstate(L), sizeof(LG), 0);  /* free main block */
}


// 释放一个线程需要,需要释放CallInfo链表以及vs栈以及它自身
void vsE_freethread (vs_State *L, vs_State *L1) {
  LX *l = fromstate(L1);
  vsF_close(L1, L1->stack);  /* close all upvalues for this thread */
  vs_assert(L1->openupval == NULL);
  freestack(L1);
  vsM_free(L, l);
}


// 创建vs_State以及global_State
VS_API vs_State *vs_newstate (vs_Alloc f, void *ud) {
  vs_State *L;
  global_State *g;
  // 调用内存分配函数分配sizeof(LG)大小的内存空间
  LG *l = cast(LG *, (*f)(ud, NULL, VS_TTHREAD, sizeof(LG)));
  if (l == NULL) return NULL;
  L = &l->l.l;
  g = &l->g;
  L->next = NULL;
  L->tt = VS_TTHREAD;
  g->currentwhite = bitmask(WHITE0BIT);
  L->marked = vsC_white(g);
  // 初始化vs_State
  preinit_thread(L, g);
  g->frealloc = f;
  g->ud = ud;
  g->mainthread = L;
  // 生成随机种子
  g->seed = makeseed(L);
  g->gcrunning = 0;  /* no GC while building state */
  g->GCestimate = 0;
  g->strt.size = g->strt.nuse = 0;
  g->strt.hash = NULL;
  setnilvalue(&g->l_registry);
  g->panic = NULL;
  g->version = NULL;
  // 初始的gcstate是GCSpause
  g->gcstate = GCSpause;
  // 初始的gc类型是正常类型,不是紧急状态
  g->gckind = KGC_NORMAL;
  g->sweepgc = NULL;
  g->gray = g->grayagain = NULL;
  g->twups = NULL;
  g->totalbytes = sizeof(LG);
  g->GCdebt = 0;
  g->gcpause = VSI_GCPAUSE;
  g->gcstepmul = VSI_GCMUL;
  // 调用f_vsopen初始化各种必要信息
  // f_vsopen中设置了gcrunning为1
  if (vsD_rawrunprotected(L, f_vsopen, NULL) != VS_OK) {
    /* memory allocation error: free partial state */
    close_state(L);
    L = NULL;
  }
  return L;
}


VS_API void vs_close (vs_State *L) {
  L = G(L)->mainthread;  /* only the main thread can be closed */
  close_state(L);
}


