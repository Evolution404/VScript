#ifndef vvm_h
#define vvm_h

#include "vobject.h"

// 数字转字符串
#define cvt2str(o)	ttisnumber(o)

// 字符串转数字
#define cvt2num(o)	ttisstring(o)


/*
** You can define VS_FLOORN2I if you want to convert floats to integers
** by flooring them (instead of raising an error if they are not
** integral values)
*/
// 定义VS_FLOORN2I可以让浮点数向下取整转换为整数
#define VS_FLOORN2I		0

// 负责将n转换成浮点数 可以转换浮点数,整数,字符串 返回值:成功为1 失败为0
// 首先检测是否是浮点数 不是浮点数进入vsV_tonumber_
#define tonumber(o,n) \
	(ttisfloat(o) ? (*(n) = fltvalue(o), 1) : vsV_tonumber_(o,n))

// 转换为整数
#define tointeger(o,i) \
    (ttisinteger(o) ? (*(i) = ivalue(o), 1) : vsV_tointeger(o,i,VS_FLOORN2I))

// ((vs_Integer)(((vs_Unsigned)(v1)) op ((vs_Unsigned)(v2))))
#define intop(op,v1,v2) l_castU2S(l_castS2U(v1) op l_castS2U(v2))


// 获取表t的k项 即t[k]
// 返回1 说明slot指向 t[k]
// 返回0 说明t不是表,slot是NULL
// f是从Table中取出第k项调用的方法 在ltable.h中定义 vsH_get vsH_getstr vsH_getint
// t是TValue类型,k是的类型由函数f的第二个参数决定,slot是TValue指针
// 查询t[k]使得slot指向结果
#define vsV_fastget(L,t,k,slot,f) \
  (!ttistable(t)  \
   ? (slot = NULL, 0)  /* not a table; 'slot' is NULL and result is 0 */  \
   : (slot = f(hvalue(t), k), 1)) /* else, do raw access hvalue从TValue中取出Table类型*/

/*
** standard implementation for 'gettable'
*/
// t,k,v都是TValue类型
// 查询t[k]写入v中
#define vsV_gettable(L,t,k,v) { const TValue *slot; \
  if (vsV_fastget(L,t,k,slot,vsH_get)) { setobj2s(L, v, slot); } \
  else vsG_typeerror(L, t, "index"); }


// t是表且t[k]不是nil 进行GC处理 让t[k]直接等于v
#define vsV_fastset(L,t,k,slot,f,v) \
  (!ttistable(t) \
   ? (slot = NULL, 0) \
   : (slot = f(hvalue(t), k), \
     ttisnil(slot) ? 0 \
     : (vsC_barrierback(L, hvalue(t), v), \
        setobj2t(L, cast(TValue *,slot), v), \
        1)))


#define vsV_settable(L,t,k,v) { const TValue *slot; \
  if (!vsV_fastset(L,t,k,slot,vsH_get,v)) \
    vsV_finishset(L,t,k,v,slot); }


VSI_FUNC int vsV_equalobj (const TValue *t1, const TValue *t2);
VSI_FUNC int vsV_lessthan (vs_State *L, const TValue *l, const TValue *r);
VSI_FUNC int vsV_lessequal (vs_State *L, const TValue *l, const TValue *r);
VSI_FUNC int vsV_tonumber_ (const TValue *obj, vs_Number *n);
VSI_FUNC int vsV_tointeger (const TValue *obj, vs_Integer *p, int mode);

VSI_FUNC void vsV_finishset (vs_State *L, const TValue *t, TValue *key,
                               StkId val, const TValue *slot);

VSI_FUNC void vsV_execute (vs_State *L);
VSI_FUNC void vsV_concat (vs_State *L, int total);
VSI_FUNC vs_Integer vsV_div (vs_State *L, vs_Integer x, vs_Integer y);
VSI_FUNC vs_Integer vsV_mod (vs_State *L, vs_Integer x, vs_Integer y);
VSI_FUNC vs_Integer vsV_shiftl (vs_Integer x, vs_Integer y);
VSI_FUNC void vsV_objlen (vs_State *L, StkId ra, const TValue *rb);

#endif
