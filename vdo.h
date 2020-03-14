/*
** $Id: ldo.h,v 2.29 2015/12/21 13:02:14 roberto Exp $
** Stack and Call structure of Lua
** See Copyright Notice in vs.h
*/

#ifndef ldo_h
#define ldo_h


#include "vobject.h"
#include "vstate.h"
#include "vzio.h"


/*
** Macro to check stack size and grow stack if needed.  Parameters
** 'pre'/'pos' allow the macro to preserve a pointer into the
** stack across reallocations, doing the work only when needed.
** 'condmovestack' is used in heavy tests to force a stack reallocation
** at every check.
*/
// 如果栈上剩余空间不足n了,就调用vsD_growstack来扩充栈上空间
// pre和pos是调用vsD_growstack之前和之后执行的语句
#define vsD_checkstackaux(L,n,pre,pos)  \
	if (L->stack_last - L->top <= (n)) \
	  { pre; vsD_growstack(L, n); pos; }

/* In general, 'pre'/'pos' are empty (nothing to save) */
#define vsD_checkstack(L,n)	vsD_checkstackaux(L,n,(void)0,(void)0)



// 先调用save再调用restore
// 在栈上指针位置被重新分配可能失效时,通过相对位置来恢复
// 例如在checkstackp中的使用
#define savestack(L,p)		((char *)(p) - (char *)L->stack)
#define restorestack(L,n)	((TValue *)((char *)L->stack + (n)))


/* type of protected functions, to be ran by 'runprotected' */
typedef void (*Pfunc) (vs_State *L, void *ud);

VSI_FUNC int vsD_protectedparser (vs_State *L, ZIO *z, const char *name,
                                                  const char *mode);
VSI_FUNC int vsD_precall (vs_State *L, StkId func, int nresults);
VSI_FUNC void vsD_call (vs_State *L, StkId func, int nResults);
VSI_FUNC int vsD_pcall (vs_State *L, Pfunc func, void *u,
                                        ptrdiff_t oldtop, ptrdiff_t ef);
VSI_FUNC int vsD_poscall (vs_State *L, CallInfo *ci, StkId firstResult,
                                          int nres);
VSI_FUNC void vsD_reallocstack (vs_State *L, int newsize);
VSI_FUNC void vsD_growstack (vs_State *L, int n);
VSI_FUNC void vsD_shrinkstack (vs_State *L);
VSI_FUNC void vsD_inctop (vs_State *L);

VSI_FUNC l_noret vsD_throw (vs_State *L, int errcode);
VSI_FUNC int vsD_rawrunprotected (vs_State *L, Pfunc f, void *ud);

#endif

