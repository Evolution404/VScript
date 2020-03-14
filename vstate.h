#ifndef vstate_h
#define vstate_h
#include "vs.h"
#include "vobject.h"


/* extra stack space to handle TM calls and some other extras */
#define EXTRA_STACK   5


#define BASIC_STACK_SIZE        (2*VS_MINSTACK)

/* kinds of Garbage Collection */
#define KGC_NORMAL	0
#define KGC_EMERGENCY	1	/* gc was forced by an allocation failure */

typedef struct stringtable {
  TString **hash;
  int nuse;  /* number of elements */
  int size;
} stringtable;


typedef struct CallInfo {
  StkId func;  /* function index in the stack */
  StkId	top;  /* top for this function */
  struct CallInfo *previous, *next;  /* dynamic call link */

  // 这里简化了共用体,只保留了针对vs函数的部分
  StkId base;  /* base for this function */
  const Instruction *savedpc;

  ptrdiff_t extra;
  short nresults;  /* expected number of results from this function */
  unsigned short callstatus;
} CallInfo;

// CIST:CallInfo status
#define CIST_VS	(1<<1)	/* call is running a VS function */
#define CIST_FRESH (1<<2)
#define CIST_TAIL	(1<<3)	/* call was tail called */

#define isVS(ci)	((ci)->callstatus & CIST_VS)

typedef struct global_State {
  vs_Alloc frealloc;  /* function to reallocate memory */
  void *ud;         /* auxiliary data to 'frealloc' */
  l_mem totalbytes;  /* number of bytes currently allocated - GCdebt */
  l_mem GCdebt;  /* bytes allocated not yet compensated by the collector */
  lu_mem GCmemtrav;  /* memory traversed by the GC */
  lu_mem GCestimate;  /* an estimate of the non-garbage memory in use */
  stringtable strt;  /* hash table for strings */
  TValue l_registry;
  unsigned int seed;  /* randomized seed for hashes */
  lu_byte currentwhite;
  lu_byte gcstate;  /* state of garbage collector */
  lu_byte gckind;  /* kind of GC running */
  lu_byte gcrunning;  /* true if GC is running */
  GCObject *allgc;  /* list of all collectable objects */
  GCObject **sweepgc;  /* current position of sweep in list */
  GCObject *gray;  /* list of gray objects */
  GCObject *grayagain;  /* list of objects to be traversed atomically */
  GCObject *fixedgc;  /* list of objects not to be collected */
  struct vs_State *twups;  /* list of threads with open upvalues */
  int gcpause;  /* size of pause between successive GCs */
  int gcstepmul;  /* GC 'granularity' */
  vs_CFunction panic;  /* to be called in unprotected errors */
  struct vs_State *mainthread;
  const vs_Number *version;  /* pointer to version number */
  TString *memerrmsg;  /* memory-error message */
  TString *strcache[STRCACHE_N][STRCACHE_M];  /* cache for strings in API */
} global_State;


struct vs_State {
  CommonHeader;
  unsigned short nci;  /* number of items in 'ci' list */
  lu_byte status;
  StkId top;  /* first free slot in the stack */
  global_State *l_G;
  CallInfo *ci;  /* call info for current function */
  const Instruction *oldpc;  /* last pc traced */
  StkId stack_last;  /* last free slot in the stack */
  StkId stack;  /* stack base */
  UpVal *openupval;  /* list of open upvalues in this stack */
  GCObject *gclist;
  struct vs_State *twups;  /* list of threads with open upvalues */
  struct vs_longjmp *errorJmp;  /* current error recover point */
  CallInfo base_ci;  /* CallInfo for first level (C calling VS) */
  ptrdiff_t errfunc;  /* current error handling function (stack index) */
  int stacksize;
  unsigned short nCcalls;  /* number of nested C calls */
};

#define G(L)	(L->l_G)


// 一个所有需要回收的类型的共用体,只用于类型转换
// GCUnion中各个项除了Closure都是以common header开始的
// 所以在强制类型转换的时候引用common header里的项不会有问题
// 例如TString以common header开头, 强制转换为GCUnion访问gc项与直接ts->gc是一样的
// 所有垃圾回收类型在创建时都调用了vsC_newobj来完成对GCObject的初始化
union GCUnion {
  GCObject gc;  /* common header */
  // createstrobj中调用vsC_newobj
  struct TString ts;
  // vsS_newudata中调用vsC_newobj
  struct Udata u;
  // vsF_newCclosure,vsF_newLclosure中都调用了vsC_newobj
  union Closure cl;
  // vsH_new中调用了vsC_newobj
  struct Table h;
  // vsF_newproto中调用了vsC_newobj
  struct Proto p;
  // vs_newstate中没调用vsC_newobj,但是对marked字段进行了处理
  struct vs_State th;  /* thread */
};


#define cast_u(o)	cast(union GCUnion *, (o))

// 用于将GCObject转换成各种数据类型
// 这里使用cast_u的目的可能是简化写法
// 例如gco2ts这里使用 &((cast_u(o))->ts) 换成 cast(TString*, o) 也是完全可以的
#define gco2ts(o)  \
	check_exp(novariant((o)->tt) == VS_TSTRING, &((cast_u(o))->ts))
#define gco2u(o)  check_exp((o)->tt == VS_TUSERDATA, &((cast_u(o))->u))
#define gco2lcl(o)  check_exp((o)->tt == VS_TLCL, &((cast_u(o))->cl.l))
#define gco2ccl(o)  check_exp((o)->tt == VS_TCCL, &((cast_u(o))->cl.c))
#define gco2cl(o)  \
	check_exp(novariant((o)->tt) == VS_TFUNCTION, &((cast_u(o))->cl))
#define gco2t(o)  check_exp((o)->tt == VS_TTABLE, &((cast_u(o))->h))
#define gco2p(o)  check_exp((o)->tt == VS_TPROTO, &((cast_u(o))->p))
#define gco2th(o)  check_exp((o)->tt == VS_TTHREAD, &((cast_u(o))->th))


/* macro to convert a VS object into a GCObject */
// 将可回收类型指针 转换成 GCObject*类型
// 先转换成gcunion再取出gcunion的gc字段
// 经过测试将 &(cast_u(v)->gc) 换成 cast(GCObject *, (v))效果是一样的
// 实质上就是将v指针的类型转换成GCObject* 不经过GCUnion进行中转也是一样的
// 其实改成 cast_u(v)编译也不会报错 只是会报出一些类型不匹配的警告
#define obj2gco(v) \
  check_exp(novariant((v)->tt) < VS_TDEADKEY, (&(cast_u(v)->gc)))
	//check_exp(novariant((v)->tt) < VS_TDEADKEY, (cast(GCObject *, (v))))


// 实际被分配的所有内存空间
#define gettotalbytes(g)	cast(lu_mem, (g)->totalbytes + (g)->GCdebt)

VSI_FUNC void vsE_setdebt (global_State *g, l_mem debt);
VSI_FUNC void vsE_freethread (vs_State *L, vs_State *L1);
VSI_FUNC CallInfo *vsE_extendCI (vs_State *L);
VSI_FUNC void vsE_freeCI (vs_State *L);
VSI_FUNC void vsE_shrinkCI (vs_State *L);

#endif
