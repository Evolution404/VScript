#ifndef lfunc_h
#define lfunc_h


#include "vobject.h"


// 计算Cclosure的大小
// CClosure定义最后一项 TValue upvalue[1] 所有的upvalue都放在最后面
// 所以计算大小就是CClosure+(n-1)个TValue的大小(CClosure中已经有了一个)
#define sizeCclosure(n)	(cast(int, sizeof(CClosure)) + \
                         cast(int, sizeof(TValue)*((n)-1)))

// 计算Lclosure的大小
// 与上面不同的是LClosure最后面存的是指针不是实际的Upvalue
// 所以是TValue的指针大小*(n-1)
#define sizeLclosure(n)	(cast(int, sizeof(LClosure)) + \
                         cast(int, sizeof(TValue *)*((n)-1)))

/* test whether thread is in 'twups' list */
// 在新建lua_State对象时设置L->twups=L,只有将该对象加入twups链表时才会不相等
// 初始化twups为L在preinit_thread中,twups链表在traversethread中链接
// 所以L与twups不相等说明L在twups链表中
#define isintwups(L)	(L->twups != L)

#define MAXUPVAL	255

// VS函数闭包变量
// 闭包变量有open和closed状态
// open状态是外层函数还没有返回 open字段用作当前作用域的闭包变量链表
// closed状态是外层函数返回将闭包变量的值拷贝出来 放在value字段
struct UpVal {
  // open状态v指向该upvalue在栈上的值
  // closed状态v指向u.value
  TValue *v;  /* points to stack or to its own value 指向变量的实际值 */
  lu_mem refcount;  /* reference counter 引用计数 */
  union {
    struct {  /* (when open) */
      UpVal *next;  /* linked list */
      int touched;  /* mark to avoid cycles with dead threads */
    } open;
    TValue value;  /* the value (when closed) */
  } u;
};

// 当v字段和value字段相等说明进入了closed状态
#define upisopen(up)	((up)->v != &(up)->u.value)

VSI_FUNC Proto *vsF_newproto (vs_State *L);
VSI_FUNC CClosure *vsF_newCclosure (vs_State *L, int nelems);
VSI_FUNC LClosure *vsF_newLclosure (vs_State *L, int nelems);
VSI_FUNC void vsF_initupvals (vs_State *L, LClosure *cl);
VSI_FUNC UpVal *vsF_findupval (vs_State *L, StkId level);
VSI_FUNC void vsF_close (vs_State *L, StkId level);
VSI_FUNC void vsF_freeproto (vs_State *L, Proto *f);
VSI_FUNC const char *vsF_getlocalname (const Proto *func, int local_number,
                                         int pc);

#endif
