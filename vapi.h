#ifndef vapi_h
#define vapi_h


#include "vlimits.h"
#include "vstate.h"

// top自增, 没有自动扩容机制
#define api_incr_top(L)   {L->top++; api_check(L, L->top <= L->ci->top, \
				"stack overflow");}

#define adjustresults(L,nres) \
    { if ((nres) == VS_MULTRET && L->ci->top < L->top) L->ci->top = L->top; }

// 检查当前栈顶的参数是否有n个
#define api_checknelems(L,n)	api_check(L, (n) < (L->top - L->ci->func), \
				  "not enough elements in the stack")


#endif
