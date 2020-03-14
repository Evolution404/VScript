#define ldo_c

#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

#include "vs.h"

#include "vapi.h"
#include "vdebug.h"
#include "vdo.h"
#include "vfunc.h"
#include "vgc.h"
#include "vmem.h"
#include "vobject.h"
#include "vopcodes.h"
#include "vparser.h"
#include "vstate.h"
#include "vstring.h"
#include "vtable.h"
#include "vundump.h"
#include "vvm.h"
#include "vzio.h"



#define errorstatus(s)	((s) > VS_YIELD)


// 错误恢复函数
#define VSI_THROW(L,c)		longjmp((c)->b, 1)
#define VSI_TRY(L,c,a)		if (setjmp((c)->b) == 0) { a }
#define vsi_jmpbuf		jmp_buf


// 错误恢复链
/* chain list of long jump buffers */
struct vs_longjmp {
  struct vs_longjmp *previous;
  vsi_jmpbuf b;
  volatile int status;  /* error code */
};


// 将错误信息设置到栈中
static void seterrorobj (vs_State *L, int errcode, StkId oldtop) {
  switch (errcode) {
    case VS_ERRMEM: {  /* memory error? */
      setsvalue2s(L, oldtop, G(L)->memerrmsg); /* reuse preregistered msg. */
      break;
    }
    case VS_ERRERR: {
      setsvalue2s(L, oldtop, vsS_newliteral(L, "error in error handling"));
      break;
    }
    default: {
      setobjs2s(L, oldtop, L->top - 1);  /* error message on current top */
      break;
    }
  }
  L->top = oldtop + 1;
}


l_noret vsD_throw (vs_State *L, int errcode) {
  // 如果有errorJmp链表,就执行longjmp跳转到调用函数的vsD_rawrunprotected中去
  if (L->errorJmp) {  /* thread has an error handler? */
    L->errorJmp->status = errcode;  /* set status */
    VSI_THROW(L, L->errorJmp);  /* jump to it */
  }
  else {  /* thread has no error handler */
    global_State *g = G(L);
    L->status = cast_byte(errcode);  /* mark it as dead */
    // 尝试使用主线程的错误恢复点
    if (g->mainthread->errorJmp) {  /* main thread has a handler? */
      setobjs2s(L, g->mainthread->top++, L->top - 1);  /* copy error obj. */
      vsD_throw(g->mainthread, errcode);  /* re-throw in main thread */
    }
    // 主线程也没有错误恢复点,执行abort结束程序
    else {  /* no handler at all; abort */
      if (g->panic) {  /* panic function? */
        seterrorobj(L, errcode, L->top);  /* assume EXTRA_STACK */
        if (L->ci->top < L->top)
          L->ci->top = L->top;  /* pushing msg. can break this invariant */
        g->panic(L);  /* call panic function (last chance to jump out) */
      }
      abort();
    }
  }
}


// 异常保护方法
// 通过回调Pfunc f,并用setjmp和longjpm方式,实现代码的中断并回到setjmp处
int vsD_rawrunprotected (vs_State *L, Pfunc f, void *ud) {
  unsigned short oldnCcalls = L->nCcalls;
  // 定义一个vs_longjmp结构体,用于保存当前执行环境
  struct vs_longjmp lj;
  // 设置状态为VS_OK
  lj.status = VS_OK;
  // 在errorJmp链表中新建一项
  lj.previous = L->errorJmp;  /* chain new error handler */
  L->errorJmp = &lj;
  // if (setjmp((c)->b) == 0) { a }
  // 这里使用了setjmp,被调用的函数f内部如果发生异常会调用vsD_throw
  // vsD_throw内部使用了VSI_THROW也就是调用了longjmp,之后会返回这里的setjmp
  // setjmp返回结果是1,不会执行函数f,也就是说函数f在执行过程中被中断了
  // 重新回到这里开始执行下一句
  VSI_TRY(L, &lj,
    (*f)(L, ud);
  );
  // 让errorJmp链表去掉上面新增的元素
  L->errorJmp = lj.previous;  /* restore old error handler */
  L->nCcalls = oldnCcalls;
  return lj.status;
}

/* }====================================================== */


/*
** {==================================================================
** Stack reallocation
** ===================================================================
*/
// 当对数据栈进行重新分配空间的时候
// 开放状态的upvalue以及callinfo的top和func指针引用的是栈上地址
// 由于空间重新分配了, 指向的地址会有变动
// correctstack就是用来修正upvalue, callinfo引用的地址
static void correctstack (vs_State *L, TValue *oldstack) {
  CallInfo *ci;
  UpVal *up;
  L->top = (L->top - oldstack) + L->stack;  // 修正top的位置
  for (up = L->openupval; up != NULL; up = up->u.open.next) // 修正upval->v的位置
    up->v = (up->v - oldstack) + L->stack;
  for (ci = L->ci; ci != NULL; ci = ci->previous) {  // 修正CallInfo中引用数据栈的地方
    ci->top = (ci->top - oldstack) + L->stack;
    ci->func = (ci->func - oldstack) + L->stack;
    if (isVS(ci))  // vs函数中base也引用了数据栈
      ci->base = (ci->base - oldstack) + L->stack;
  }
}


/* some space for error handling */
#define ERRORSTACKSIZE	(VSI_MAXSTACK + 200)


// 设置数据栈的大小为newsize(包括EXTRA_STACK)
// 修改 stacksize, stack, stack_last
// 使用correctstack修正top up和ci
void vsD_reallocstack (vs_State *L, int newsize) {
  TValue *oldstack = L->stack;
  int lim = L->stacksize;
  vs_assert(newsize <= VSI_MAXSTACK || newsize == ERRORSTACKSIZE);
  vs_assert(L->stack_last - L->stack == L->stacksize - EXTRA_STACK);
  // 重新分配栈的空间
  vsM_reallocvector(L, L->stack, L->stacksize, newsize, TValue);
  for (; lim < newsize; lim++)   // 为增加的位置初始化
    setnilvalue(L->stack + lim); /* erase new segment */
  L->stacksize = newsize;        // 修正相关变量
  L->stack_last = L->stack + newsize - EXTRA_STACK;  // stack_last不包括EXTRA_STACK
  correctstack(L, oldstack);  // 修正top up以及ci
}


// 扩张vs栈的空间 一般让空间翻倍如果翻倍后剩余空间不足n那么就扩张到剩余空间为n
// n是执行完毕后栈上最少剩余的空间
void vsD_growstack (vs_State *L, int n) {
  int size = L->stacksize;
  if (size > VSI_MAXSTACK)  /* error after extra size? */
    vsD_throw(L, VS_ERRERR);
  else {
    // 新大小实际上是 min(max(旧大小*2,至少需要的大小), MAXSTACK)
    int needed = cast_int(L->top - L->stack) + n + EXTRA_STACK;
    int newsize = 2 * size;
    if (newsize > VSI_MAXSTACK) newsize = VSI_MAXSTACK;
    if (newsize < needed) newsize = needed;
    if (newsize > VSI_MAXSTACK) {  /* stack overflow? */
      vsD_reallocstack(L, ERRORSTACKSIZE);
      vsG_runerror(L, "stack overflow");
    }
    else
      vsD_reallocstack(L, newsize); // 重新分配栈的空间
  }
}


// 返回的是CallInfo链上最前的那个函数在栈上使用的个数
static int stackinuse (vs_State *L) {
  CallInfo *ci;
  StkId lim = L->top;
  for (ci = L->ci; ci != NULL; ci = ci->previous) {
    if (lim < ci->top) lim = ci->top;
  }
  vs_assert(lim <= L->stack_last);
  return cast_int(lim - L->stack) + 1;  /* part of stack in use */
}


// 检测当前栈使用量是否超过了限制
// 如果goodsize更小对栈的大小重新分配为goodsize
// 疑问: stackinuse是最前的ci的使用量, 分配为goodsize只能保证最前的callinfo
// 其他的callinfo是如何处理的, callinfo链是如何应用的
void vsD_shrinkstack (vs_State *L) {
  int inuse = stackinuse(L);
  int goodsize = inuse + (inuse / 8) + 2*EXTRA_STACK;
  if (goodsize > VSI_MAXSTACK)
    goodsize = VSI_MAXSTACK;  /* respect stack limit */
  if (L->stacksize > VSI_MAXSTACK)  /* had been handling stack overflow? */
    vsE_freeCI(L);  /* free all CIs (list grew because of an error) */
  else
    vsE_shrinkCI(L);  /* shrink list */
  /* if thread is currently not handling a stack overflow and its
     good size is smaller than current size, shrink its stack */
  if (inuse <= (VSI_MAXSTACK - EXTRA_STACK) &&
      goodsize < L->stacksize)
    vsD_reallocstack(L, goodsize);
}


void vsD_inctop (vs_State *L) {
  // 扩展为 if (L->stack_last - L->top <= (1)) { (void)0; vsD_growstack(L, 1); (void)0; } else { ((void)0); }
  vsD_checkstack(L, 1);
  L->top++;
}

/* }================================================================== */


// 为可变参数函数设置栈环境
// actual是函数调用时实际传入的参数个数
static StkId adjust_varargs (vs_State *L, Proto *p, int actual) {
  int i;
  // fixedarg代表定义函数时...前面的变量个数
  int nfixargs = p->numparams;
  StkId base, fixed;
  /* move fixed parameters to final position */
  fixed = L->top - actual;  /* first fixed argument */
  base = L->top;  /* final position of first argument */
  // 将固定参数移动到后面,可变参数保持原地不动
  for (i = 0; i < nfixargs && i < actual; i++) {
    setobjs2s(L, L->top++, fixed + i);
    setnilvalue(fixed + i);  /* erase original copy (for GC) */
  }
  for (; i < nfixargs; i++)
    setnilvalue(L->top++);  /* complete missing arguments */
  return base;
}


/*
** Given 'nres' results at 'firstResult', move 'wanted' of them to 'res'.
** Handle most typical cases (zero results for commands, one result for
** expressions, multiple results for tail calls/single parameters)
** separated.
*/
// firstResult是当前保存返回值的第一个位置
// res是需要拷贝的目标位置
// nres是真实的返回值个数,wanted是调用时需要的返回值个数
// 由于nres是通过栈上真实值计算出来所以是确定的值,wanted可能是确定个数也可能是多返回值
// 返回值是 wanted != VS_MULTRET
static int moveresults (vs_State *L, const TValue *firstResult, StkId res,
                                      int nres, int wanted) {
  switch (wanted) {  /* handle typical cases separately */
    // 可能是为了提高效率对最典型的0和1进行特殊处理
    case 0: break;  /* nothing to move */
    case 1: {  /* one result needed */
      // 没有返回值但是需要一个值 赋值为nil
      if (nres == 0)   /* no results? */
        firstResult = vsO_nilobject;  /* adjust with nil */
      setobjs2s(L, res, firstResult);  /* move it to proper place */
      break;
    }
    // 接收的值个数不定,保存所有返回结果
    case VS_MULTRET: {
      int i;
      for (i = 0; i < nres; i++)  /* move all results to correct place */
        setobjs2s(L, res + i, firstResult + i);
      // 正常情况L->top = res + wanted
      // 但是多返回值wanted是-1,所以就按照真实返回的个数设置结果
      L->top = res + nres;
      return 0;  /* wanted == VS_MULTRET */
    }
    default: {
      int i;
      // 需要的值小于等于返回的值 直接拷贝需要的个数就行了
      if (wanted <= nres) {  /* enough results? */
        for (i = 0; i < wanted; i++)  /* move wanted results to correct place */
          setobjs2s(L, res + i, firstResult + i);
      }
      // 需要的值多于返回的值 多余的值需要设置为nil
      else {  /* not enough results; use all of them plus nils */
        for (i = 0; i < nres; i++)  /* move all results to correct place */
          setobjs2s(L, res + i, firstResult + i);
        for (; i < wanted; i++)  /* complete wanted number of results */
          setnilvalue(res + i);
      }
      break;
    }
  }
  L->top = res + wanted;  /* top points after the last result */
  return 1;
}


/*
** Finishes a function call: calls hook if necessary, removes CallInfo,
** moves current number of results to proper place; returns 0 iff call
** wanted multiple (variable number of) results.
*/
// 该函数让CallInfo返回上一层,并重新设置栈上数据,正确设置返回值
// 如果vs函数调用处需要多返回值返回0,否则返回1
int vsD_poscall (vs_State *L, CallInfo *ci, StkId firstResult, int nres) {
  StkId res;
  // wanted代表外部调用该函数处需要的结果的个数
  int wanted = ci->nresults;
  // 最终第一个返回值就保存在被调用的函数的位置上
  res = ci->func;  /* res == final position of 1st result */
  L->ci = ci->previous;  /* back to caller */
  /* move results to proper place */
  return moveresults(L, firstResult, res, nres, wanted);
}



#define next_ci(L) (L->ci = (L->ci->next ? L->ci->next : vsE_extendCI(L)))


/* macro to check stack size, preserving 'p' */
// 保证栈上还有剩余n个空间,并保护栈上指针p
// 重新申请空间之后p的指针位置可能失效,所以先保存相对位置,然后再通过相对位置恢复
#define checkstackp(L,n,p)  \
  vsD_checkstackaux(L, n, \
    ptrdiff_t t__ = savestack(L, p);  /* save 'p' */ \
    vsC_checkGC(L),  /* stack grow uses memory */ \
    p = restorestack(L, t__))  /* 'pos' part: restore 'p' */


/*
** Prepares a function call: checks the stack, creates a new CallInfo
** entry, fills in the relevant information, calls hook if needed.
** If function is a C function, does the call, too. (Otherwise, leave
** the execution ('vsV_execute') to the caller, to allow stackless
** calls.) Returns true iff function has been executed (C function).
*/
// 如果传入c函数 直接执行
// 传入vs函数   进行执行前的准备
int vsD_precall (vs_State *L, StkId func, int nresults) {
  vs_CFunction f;
  CallInfo *ci;
  switch (ttype(func)) {
    case VS_TCCL:  /* C closure */
      f = clCvalue(func)->f;
      goto Cfunc;
    case VS_TLCF:  /* light C function */
      f = fvalue(func);
     Cfunc: {
      int n;  /* number of returns */
      checkstackp(L, VS_MINSTACK, func);  /* ensure minimum stack size */
      // L->ci = L->ci->next; ci=L->ci
      ci = next_ci(L);  /* now 'enter' new function */
      // 初始化要执行的函数的CallInfo
      // 传入的nresults代表调用处需要的返回结果个数,也就是wanted
      ci->nresults = nresults;
      ci->func = func;
      ci->top = L->top + VS_MINSTACK;
      vs_assert(ci->top <= L->stack_last);
      ci->callstatus = 0;
      // 真正调用函数,返回值n代表nres,也就是真实的返回值个数
      // 函数调用后栈空间如下所示,分别是被调用的函数,函数参数,函数执行的返回结果
      // func arg1 arg2 res1 res2
      n = (*f)(L);  /* do the actual call */
      api_checknelems(L, n);
      // n是真实返回值个数,L->top-n就代表第一个返回值保存的位置
      vsD_poscall(L, ci, L->top - n, n);
      return 1;
    }
    case VS_TLCL: {  /* VS function: prepare its call */
      StkId base;
      Proto *p = clLvalue(func)->p;
      int n = cast_int(L->top - func) - 1;  /* number of real arguments */
      // maxstacksize是函数调用过程中最多占用的寄存器个数
      int fsize = p->maxstacksize;  /* frame size */
      // 扩充栈上空间并保护func的位置
      checkstackp(L, fsize, func);
      if (p->is_vararg)
        base = adjust_varargs(L, p, n);
      else {  /* non vararg function */
        // 如果传入的参数不够需要的参数,剩余部分设置为nil
        for (; n < p->numparams; n++)
          setnilvalue(L->top++);  /* complete missing arguments */
        base = func + 1;
      }
      ci = next_ci(L);  /* now 'enter' new function */
      ci->nresults = nresults;
      ci->func = func;
      ci->base = base;
      L->top = ci->top = base + fsize;
      vs_assert(ci->top <= L->stack_last);
      ci->savedpc = p->code;  /* starting point */
      // callinfo的状态是在调用vs函数
      ci->callstatus = CIST_VS;
      return 0;
    }
    default: {  /* not a function */
      vsG_typeerror(L, func, "call");
    }
  }
}


/*
** Check appropriate error for stack overflow ("regular" overflow or
** overflow while handling stack overflow). If 'nCalls' is larger than
** VSI_MAXCCALLS (which means it is handling a "regular" overflow) but
** smaller than 9/8 of VSI_MAXCCALLS, does not report an error (to
** allow overflow handling to work)
*/
static void stackerror (vs_State *L) {
  if (L->nCcalls == VSI_MAXCCALLS)
    vsG_runerror(L, "C stack overflow");
  else if (L->nCcalls >= (VSI_MAXCCALLS + (VSI_MAXCCALLS>>3)))
    vsD_throw(L, VS_ERRERR);  /* error while handing stack error */
}


/*
** Call a function (C or VS). The function to be called is at *func.
** The arguments are on the stack, right after the function.
** When returns, all the results are on the stack, starting at the original
** function position.
*/
// 调用一个函数
void vsD_call (vs_State *L, StkId func, int nResults) {
  if (++L->nCcalls >= VSI_MAXCCALLS)  // c函数调用栈深度达到极限了
    stackerror(L);
  if (!vsD_precall(L, func, nResults))  /* is a VS function? */
    vsV_execute(L);  /* call it */
  L->nCcalls--;
}


int vsD_pcall (vs_State *L, Pfunc func, void *u,
                ptrdiff_t old_top, ptrdiff_t ef) {
  int status;
  CallInfo *old_ci = L->ci;
  ptrdiff_t old_errfunc = L->errfunc;
  L->errfunc = ef;
  status = vsD_rawrunprotected(L, func, u);
  // 如果函数执行失败,CallInfo需要回滚到之前的状态
  if (status != VS_OK) {  /* an error occurred? */
    StkId oldtop = restorestack(L, old_top);
    vsF_close(L, oldtop);  /* close possible pending closures */
    seterrorobj(L, status, oldtop);
    L->ci = old_ci;
    vsD_shrinkstack(L);
  }
  L->errfunc = old_errfunc;
  return status;
}



/*
** Execute a protected parser.
*/
// 调用f_parser需要的数据
struct SParser {  /* data to 'f_parser' */
  ZIO *z;
  Mbuffer buff;  /* dynamic structure used by the scanner */
  Dyndata dyd;  /* dynamic structures used by the parser */
  const char *mode;
  const char *name;
};

// 只是判断mode与x是否相等
// 应该是为了提高效率没有使用strcmp
static void checkmode (vs_State *L, const char *mode, const char *x) {
  if (mode && strchr(mode, x[0]) == NULL) {
    vsO_pushfstring(L,
       "attempt to load a %s chunk (mode is '%s')", x, mode);
    vsD_throw(L, VS_ERRSYNTAX);
  }
}

// 二进制文件: vsU_undump
// 文本文件: vsY_parser
static void f_parser (vs_State *L, void *ud) {
  LClosure *cl;
  struct SParser *p = cast(struct SParser *, ud);
  int c = zgetc(p->z);  /* read first character */
  if (c == VS_SIGNATURE[0]) {
    checkmode(L, p->mode, "binary");
    cl = vsU_undump(L, p->z, p->name);
  }
  else {
    checkmode(L, p->mode, "text");
    cl = vsY_parser(L, p->z, &p->buff, &p->dyd, p->name, c);
  }
  vs_assert(cl->nupvalues == cl->p->sizeupvalues);
  // 初始化主函数的upvalue也就是_ENV变量
  vsF_initupvals(L, cl);
}

// 初始化一个SParser对象给f_parser使用
// 调用f_parser后释放必要的空间
int vsD_protectedparser (vs_State *L, ZIO *z, const char *name,
                                        const char *mode) {
  struct SParser p;
  int status;
  p.z = z; p.name = name; p.mode = mode;
  p.dyd.actvar.arr = NULL; p.dyd.actvar.size = 0;
  p.dyd.gt.arr = NULL; p.dyd.gt.size = 0;
  p.dyd.label.arr = NULL; p.dyd.label.size = 0;
  vsZ_initbuffer(L, &p.buff);
  // 调用f_parser
  status = vsD_pcall(L, f_parser, &p, savestack(L, L->top), L->errfunc);
  // 释放使用的空间
  vsZ_freebuffer(L, &p.buff);
  vsM_freearray(L, p.dyd.actvar.arr, p.dyd.actvar.size);
  vsM_freearray(L, p.dyd.gt.arr, p.dyd.gt.size);
  vsM_freearray(L, p.dyd.label.arr, p.dyd.label.size);
  return status;
}


