#define lapi_c

#include <stdarg.h>
#include <string.h>

#include "vs.h"
#include "vapi.h"
#include "vdebug.h"
#include "vdo.h"
#include "vfunc.h"
#include "vgc.h"
#include "vmem.h"
#include "vobject.h"
#include "vstate.h"
#include "vstring.h"
#include "vtable.h"
#include "vundump.h"
#include "vvm.h"


static const char udatatypename[] = "userdata";
static const char *const vs_typenames_[VS_TOTALTAGS] = {
  "no value",
  "nil", "boolean", udatatypename, "number",
  "string", "table", "function", udatatypename, "thread",
  "proto" /* this last case is used for tests only */
};

/* value at a non-valid index */
#define NONVALIDVALUE		cast(TValue *, vsO_nilobject)

/* corresponding test */
#define isvalid(o)	((o) != vsO_nilobject)

/* test for pseudo index */
#define ispseudo(i)		((i) <= VS_REGISTRYINDEX)

/* test for upvalue */
#define isupvalue(i)		((i) < VS_REGISTRYINDEX)

/* test for valid but not pseudo index */
#define isstackindex(i, o)	(isvalid(o) && !ispseudo(i))

#define api_checkvalidindex(l,o)  api_check(l, isvalid(o), "invalid index")

#define api_checkstackindex(l, i, o)  \
	api_check(l, isstackindex(i, o), "index not in the stack")


// 通过idx取栈上的值,idx不能为0
// func func+1      func+2     ... top-1        top
//         1           2       ... top-func-1 top-func
//      func+1-top  func+2-top       -1          0
// idx>0 L->ci->func+idx
// -1000000-1000<idx<=0 L->top+idx
// idx=-1000000-1000 返回注册表
// idx<-1000000-1000 去upvalues中查找
static TValue *index2addr (vs_State *L, int idx) {
  CallInfo *ci = L->ci;
  if (idx > 0) {  // 返回ci->func+idx
    TValue *o = ci->func + idx;
    // 确保取的值在ci->top前
    api_check(L, idx <= ci->top - (ci->func + 1), "unacceptable index");
    if (o >= L->top) return NONVALIDVALUE;  // 超过L->top无效
    else return o;
  }
  else if (!ispseudo(idx)) {  /* negative index -1000000-1000<idx<=0  */
    // 确保func+1<= L->top + idx <top
    api_check(L, idx != 0 && -idx <= L->top - (ci->func + 1), "invalid index");
    // -1 得到 top-1 最小取到 func+1
    return L->top + idx;
  }
  else if (idx == VS_REGISTRYINDEX)  // 访问伪索引返回全局注册表
    return &G(L)->l_registry;
  else {  /* upvalues 取upvalues idx < -1000000-1000 */
    idx = VS_REGISTRYINDEX - idx;  // 处理idx变成正数
    api_check(L, idx <= MAXUPVAL + 1, "upvalue index too large");
    if (ttislcf(ci->func))  /* light C function? */
      return NONVALIDVALUE;  /* it has no upvalues 纯c函数没有upvalues */
    else {
      CClosure *func = clCvalue(ci->func);
      return (idx <= func->nupvalues) ? &func->upvalue[idx-1] : NONVALIDVALUE;  // 返回upvalue
    }
  }
}


/*
** to be called by 'vs_checkstack' in protected mode, to grow stack
** capturing memory errors
*/
static void growstack (vs_State *L, void *ud) {
  int size = *(int *)ud;
  vsD_growstack(L, size);
}


// 确保栈上至少还有n个空间 成功返回1 失败返回0
VS_API int vs_checkstack (vs_State *L, int n) {
  int res;
  CallInfo *ci = L->ci;
  api_check(L, n >= 0, "negative 'n'");
  if (L->stack_last - L->top > n)  /* stack large enough? 本来就有n个以上空间 */
    res = 1;  /* yes; check is OK */
  else {  /* no; need to grow stack 空间不足需要重新分配 */
    int inuse = cast_int(L->top - L->stack) + EXTRA_STACK;
    if (inuse > VSI_MAXSTACK - n)  /* can grow without overflow? 不能增加了 */
      res = 0;  /* no */
    else  /* try to grow stack 扩张栈空间 */
      res = (vsD_rawrunprotected(L, &growstack, &n) == VS_OK);
  }
  if (res && ci->top < L->top + n)
    ci->top = L->top + n;  /* adjust frame top */
  return res;
}

VS_API vs_CFunction vs_atpanic (vs_State *L, vs_CFunction panicf) {
  vs_CFunction old;
  old = G(L)->panic;
  G(L)->panic = panicf;
  return old;
}


// version是一个静态变量,全局唯一保存一个version变量
VS_API const vs_Number *vs_version (vs_State *L) {
  static const vs_Number version = VS_VERSION_NUM;
  if (L == NULL) return &version;
  else return G(L)->version;
}



/*
** basic stack manipulation
*/


/*
** convert an acceptable stack index into an absolute index
*/
// 如下是正负数的对应关系
//   func func+1      func+2     ... top-1        top
// +         1           2       ... top-func-1 top-func
// -      func+1-top  func+2-top       -1          0
// -1对应的是top-func-1 也就是idx<0转换成正数是 top-func+idx
VS_API int vs_absindex (vs_State *L, int idx) {
  return (idx > 0 || ispseudo(idx))
         ? idx
         : cast_int(L->top - L->ci->func) + idx;
}


// 返回的是top-1位置的idx
VS_API int vs_gettop (vs_State *L) {
  return cast_int(L->top - (L->ci->func + 1));
}


// 设置top的位置
// 正数: top = func+1+idx  传入0 top指向func+1
// 负数: top = top+1+idx   传入-1 top指向top 传入-2 top指向top-1
VS_API void vs_settop (vs_State *L, int idx) {
  StkId func = L->ci->func;
  if (idx >= 0) {  // 输入正数
    // 确保top最小到func+1 最大到stack_last
    api_check(L, idx <= L->stack_last - (func + 1), "new top too large");
    while (L->top < (func + 1) + idx)  // 如果栈扩张 新增的数据都是nil
      setnilvalue(L->top++);
    L->top = (func + 1) + idx;  // 设置top的位置
  }
  else {  // 负数
    // 确保top最小移动到func+1
    api_check(L, -(idx+1) <= (L->top - (func + 1)), "invalid new top");
    L->top += idx+1;  /* 'subtract' index (index is negative) */
  }
}


/*
** Reverse the stack segment from 'from' to 'to'
** (auxiliary to 'vs_rotate')
*/
// 将栈上from和to之间的值反转 vs_rotate的辅助函数
// 例如 a b c 执行后 c b a
static void reverse (vs_State *L, StkId from, StkId to) {
  for (; from < to; from++, to--) {
    TValue temp;
    setobj(L, &temp, from);  // temp = from
    setobjs2s(L, from, to);  // from = to
    setobj2s(L, to, &temp);  // to = temp
  }
}


/*
** Let x = AB, where A is a prefix of length 'n'. Then,
** rotate x n == BA. But BA == (A^r . B^r)^r.
*/
// 将从idx开始到栈顶的值进行左移(n>0)或右移(n<0)
// 假设从idx开始到最后是12345
// 当输入n=3  结果为34512 右移了3位
// 当输入n=-3 结果为45123 左移了3位
VS_API void vs_rotate (vs_State *L, int idx, int n) {
  StkId p, t, m;
  // t是要移动的开始位置
  t = L->top - 1;  /* end of stack segment being rotated */
  // p是要移动的结束位置
  p = index2addr(L, idx);  /* start of segment */
  api_checkstackindex(L, idx, p);
  api_check(L, (n >= 0 ? n : -n) <= (t - p + 1), "invalid 'n'");
  // m是两次反转的分割位置
  m = (n >= 0 ? t - n : p - n - 1);  /* end of prefix */
  // 例如12345右移3位 p指向1 m指向2
  // 反转p和m之间:21345
  reverse(L, p, m);  /* reverse the prefix with length 'n' */
  // 反转m+1和t之间:21543
  reverse(L, m + 1, t);  /* reverse the suffix */
  // 反转p和t之间:34512
  reverse(L, p, t);  /* reverse the entire segment */
}


// 设置toidx的值位fromidx
VS_API void vs_copy (vs_State *L, int fromidx, int toidx) {
  TValue *fr, *to;
  fr = index2addr(L, fromidx);
  to = index2addr(L, toidx);
  api_checkvalidindex(L, to);
  setobj(L, to, fr);
  if (isupvalue(toidx))  /* function upvalue? */
    vsC_barrier(L, clCvalue(L->ci->func), fr);
  /* VS_REGISTRYINDEX does not need gc barrier
     (collector revisits it before finishing collection) */
}


// 将idx位置值压入栈顶
VS_API void vs_pushvalue (vs_State *L, int idx) {
  setobj2s(L, L->top, index2addr(L, idx));
  api_incr_top(L);
}



/*
** access functions (stack -> C)
*/


VS_API int vs_type (vs_State *L, int idx) {
  StkId o = index2addr(L, idx);
  return (isvalid(o) ? ttnov(o) : VS_TNONE);
}


VS_API const char *vs_typename (vs_State *L, int t) {
  UNUSED(L);
  api_check(L, VS_TNONE <= t && t < VS_NUMTAGS, "invalid tag");
  return vs_typenames_[t+1];
}


VS_API int vs_iscfunction (vs_State *L, int idx) {
  StkId o = index2addr(L, idx);
  return (ttislcf(o) || (ttisCclosure(o)));
}


VS_API int vs_isinteger (vs_State *L, int idx) {
  StkId o = index2addr(L, idx);
  return ttisinteger(o);
}


VS_API int vs_isnumber (vs_State *L, int idx) {
  vs_Number n;
  const TValue *o = index2addr(L, idx);
  return tonumber(o, &n);
}


VS_API int vs_isstring (vs_State *L, int idx) {
  const TValue *o = index2addr(L, idx);
  return (ttisstring(o) || cvt2str(o));
}


VS_API int vs_isuserdata (vs_State *L, int idx) {
  const TValue *o = index2addr(L, idx);
  return (ttisfulluserdata(o) || ttislightuserdata(o));
}


// 将栈顶的1个或2个操作数执行op运算 删除操作数栈顶保留运算结果
VS_API void vs_arith (vs_State *L, int op) {
  if (op != VS_OPUNM && op != VS_OPBNOT)  // 取负和按位取反是一元运算
    // 不是取负和按位取反都需要两个参数
    api_checknelems(L, 2);  /* all other operations expect two operands */
  else {  /* for unary operations, add fake 2nd operand */
    // 一元运算在栈上拷贝一个值模拟成二元运算
    api_checknelems(L, 1);
    setobjs2s(L, L->top, L->top - 1);
    api_incr_top(L);
  }
  /* first operand at top - 2, second at top - 1; result go to top - 2 */
  // top -2 = top-2 op top-1
  vsO_arith(L, op, L->top - 2, L->top - 1, L->top - 2);
  // 栈顶只保留运算结果
  L->top--;  /* remove second operand */
}


// 对index1和index2的值进行相等,小于,小于等于的比较
VS_API int vs_compare (vs_State *L, int index1, int index2, int op) {
  StkId o1, o2;
  int i = 0;
  o1 = index2addr(L, index1);
  o2 = index2addr(L, index2);
  if (isvalid(o1) && isvalid(o2)) {
    switch (op) {
      case VS_OPEQ: i = vsV_equalobj(o1, o2); break;  // == L是否为NULL决定是否查询元表
      case VS_OPLT: i = vsV_lessthan(L, o1, o2); break;  // <
      case VS_OPLE: i = vsV_lessequal(L, o1, o2); break; // <=
      default: api_check(L, 0, "invalid option");
    }
  }
  return i;
}


VS_API size_t vs_stringtonumber (vs_State *L, const char *s) {
  size_t sz = vsO_str2num(s, L->top);
  if (sz != 0)
    api_incr_top(L);
  return sz;
}


VS_API vs_Number vs_tonumberx (vs_State *L, int idx, int *pisnum) {
  vs_Number n;
  const TValue *o = index2addr(L, idx);
  int isnum = tonumber(o, &n);
  if (!isnum)
    n = 0;  /* call to 'tonumber' may change 'n' even if it fails */
  if (pisnum) *pisnum = isnum;
  return n;
}


VS_API vs_Integer vs_tointegerx (vs_State *L, int idx, int *pisnum) {
  vs_Integer res;
  const TValue *o = index2addr(L, idx);
  int isnum = tointeger(o, &res);
  if (!isnum)
    res = 0;  /* call to 'tointeger' may change 'n' even if it fails */
  if (pisnum) *pisnum = isnum;
  return res;
}


VS_API int vs_toboolean (vs_State *L, int idx) {
  const TValue *o = index2addr(L, idx);
  return !l_isfalse(o);
}


// 将栈上值转换成字符串 设置len为字符串长度
// 返回转换后字符串的地址
VS_API const char *vs_tolstring (vs_State *L, int idx, size_t *len) {
  StkId o = index2addr(L, idx);
  if (!ttisstring(o)) {
    if (!cvt2str(o)) {  /* not convertible? 检测能不能转换成字符串 */
      if (len != NULL) *len = 0;
      return NULL;
    }
    vsO_tostring(L, o);
    vsC_checkGC(L);
    o = index2addr(L, idx);  /* previous call may reallocate the stack */
  }
  if (len != NULL)
    *len = vslen(o);
  return svalue(o);
}


// 直接返回对象的长度
VS_API size_t vs_rawlen (vs_State *L, int idx) {
  StkId o = index2addr(L, idx);
  switch (ttype(o)) {
    case VS_TSHRSTR: return tsvalue(o)->shrlen;
    case VS_TLNGSTR: return tsvalue(o)->u.lnglen;
    case VS_TUSERDATA: return uvalue(o)->len;
    case VS_TTABLE: return vsH_getn(hvalue(o));
    default: return 0;
  }
}


VS_API vs_CFunction vs_tocfunction (vs_State *L, int idx) {
  StkId o = index2addr(L, idx);
  if (ttislcf(o)) return fvalue(o);
  else if (ttisCclosure(o))
    return clCvalue(o)->f;
  else return NULL;  /* not a C function */
}


VS_API void *vs_touserdata (vs_State *L, int idx) {
  StkId o = index2addr(L, idx);
  switch (ttnov(o)) {
    case VS_TUSERDATA: return getudatamem(uvalue(o));
    case VS_TLIGHTUSERDATA: return pvalue(o);
    default: return NULL;
  }
}


VS_API vs_State *vs_tothread (vs_State *L, int idx) {
  StkId o = index2addr(L, idx);
  return (!ttisthread(o)) ? NULL : thvalue(o);
}


// 返回栈上值 实际存储位置的指针
// 就是利用print函数打印出来显示的指针
VS_API const void *vs_topointer (vs_State *L, int idx) {
  StkId o = index2addr(L, idx);
  switch (ttype(o)) {
    case VS_TTABLE: return hvalue(o);
    case VS_TLCL: return clLvalue(o);
    case VS_TCCL: return clCvalue(o);
    case VS_TLCF: return cast(void *, cast(size_t, fvalue(o)));
    case VS_TTHREAD: return thvalue(o);
    case VS_TUSERDATA: return getudatamem(uvalue(o));
    case VS_TLIGHTUSERDATA: return pvalue(o);
    default: return NULL;
  }
}



/*
** push functions (C -> stack)
*/


// 栈顶设置为nil
VS_API void vs_pushnil (vs_State *L) {
  setnilvalue(L->top);
  api_incr_top(L);
}


// 栈顶设置为浮点数
VS_API void vs_pushnumber (vs_State *L, vs_Number n) {
  setfltvalue(L->top, n);
  api_incr_top(L);
}


// 栈顶设置整数
VS_API void vs_pushinteger (vs_State *L, vs_Integer n) {
  setivalue(L->top, n);
  api_incr_top(L);
}


/*
** Pushes on the stack a string with given length. Avoid using 's' when
** 'len' == 0 (as 's' can be NULL in that case), due to later use of
** 'memcmp' and 'memcpy'.
*/
// 栈顶设置值为s字符串 调用指定长度 返回虚拟存放字符串地址
VS_API const char *vs_pushlstring (vs_State *L, const char *s, size_t len) {
  TString *ts;
  ts = (len == 0) ? vsS_new(L, "") : vsS_newlstr(L, s, len);
  setsvalue2s(L, L->top, ts);
  api_incr_top(L);
  vsC_checkGC(L);
  return getstr(ts);
}


// 栈顶设置值为s字符串 不指定长度 返回虚拟存放字符串地址
VS_API const char *vs_pushstring (vs_State *L, const char *s) {
  if (s == NULL)
    setnilvalue(L->top);
  else {
    TString *ts;
    ts = vsS_new(L, s);
    setsvalue2s(L, L->top, ts);
    s = getstr(ts);  /* internal copy's address */
  }
  api_incr_top(L);
  vsC_checkGC(L);
  return s;
}


// 将格式化字符串放入栈顶 返回格式化后的字符串
VS_API const char *vs_pushvfstring (vs_State *L, const char *fmt,
                                      va_list argp) {
  const char *ret;
  ret = vsO_pushvfstring(L, fmt, argp);
  vsC_checkGC(L);
  return ret;
}


// 同上 调用方式不同
// 将格式化字符串放入栈顶 返回格式化后的字符串
VS_API const char *vs_pushfstring (vs_State *L, const char *fmt, ...) {
  const char *ret;
  va_list argp;
  va_start(argp, fmt);
  ret = vsO_pushvfstring(L, fmt, argp);
  va_end(argp);
  vsC_checkGC(L);
  return ret;
}


// 栈顶设置一个c函数闭包 n是upvalue的个数
// 执行前n个upvalue都放在栈顶 执行后n个upvalue拷贝到新建的CClosure中
// 清除栈上n个upvalue, 栈顶向前移动n个, 压入CClosure 栈顶自增
VS_API void vs_pushcclosure (vs_State *L, vs_CFunction fn, int n) {
  if (n == 0) {
    setfvalue(L->top, fn);
  }
  else {
    CClosure *cl;
    api_checknelems(L, n);
    api_check(L, n <= MAXUPVAL, "upvalue index too large");
    cl = vsF_newCclosure(L, n);  // 新建c函数闭包
    cl->f = fn;
    L->top -= n;  // n个upvalue移出栈
    // 拷贝栈上的n个upvalue到cl中
    while (n--) {
      setobj2n(L, &cl->upvalue[n], L->top + n);
      /* does not need barrier because closure is white */
    }
    setclCvalue(L, L->top, cl);  // c函数闭包cl压入栈顶
  }
  api_incr_top(L);
  vsC_checkGC(L);
}


// 将布尔值压入栈顶 压入的值一定是0或1
VS_API void vs_pushboolean (vs_State *L, int b) {
  setbvalue(L->top, (b != 0));  /* ensure that true is 1 将非0值都转换成1 */
  api_incr_top(L);
}


// 将用户数据压入栈顶
VS_API void vs_pushlightuserdata (vs_State *L, void *p) {
  setpvalue(L->top, p);
  api_incr_top(L);
}


// 将传入线程压入栈顶
// 返回主线程是否是传入的线程
VS_API int vs_pushthread (vs_State *L) {
  setthvalue(L, L->top, L);
  api_incr_top(L);
  return (G(L)->mainthread == L);
}



/*
** get functions (VS -> stack)
*/


// 从表t中查询k项 返回查询结果元素的类型
// 先直接查询表项,查询不到使用元方法查询 设置栈顶为查询结果
static int auxgetstr (vs_State *L, const TValue *t, const char *k) {
  TString *str = vsS_new(L, k);
  setsvalue2s(L, L->top, str);
  api_incr_top(L);
  vsV_gettable(L, t, L->top-1, L->top-1);
  return ttnov(L->top - 1);  // 返回栈顶的类型
}


// 设置 _G[name] 到栈顶
// 返回查询结果的类型
VS_API int vs_getglobal (vs_State *L, const char *name) {
  Table *reg = hvalue(&G(L)->l_registry);
  return auxgetstr(L, vsH_getint(reg, VS_RIDX_GLOBALS), name);
}


// t为idx位置的表
// top-1=t[top-1] 修改栈顶的值为表中项 返回查询结果的类型
VS_API int vs_gettable (vs_State *L, int idx) {
  StkId t;
  t = index2addr(L, idx);
  vsV_gettable(L, t, L->top - 1, L->top - 1);
  return ttnov(L->top - 1);
}


// 查询idx位置值的k键的值 压入栈顶 返回查询结果的类型
// 与上一个函数区别是指定了要查询的key 并且将结果压入栈
// 上面的函数没有指定key 结果直接替换了栈顶被使用的key
VS_API int vs_getfield (vs_State *L, int idx, const char *k) {
  return auxgetstr(L, index2addr(L, idx), k);
}


// 设置top-1 = t[n]
VS_API int vs_geti (vs_State *L, int idx, vs_Integer n) {
  StkId t;
  const TValue *slot;
  t = index2addr(L, idx);
  if (vsV_fastget(L, t, n, slot, vsH_getint)) {
    setobj2s(L, L->top, slot);
    api_incr_top(L);
  }
  else vsG_typeerror(L, t, "index");

  return ttnov(L->top - 1);
}


// top-1 = t[p] 查询结果压入栈顶,不查询元表
VS_API int vs_getp (vs_State *L, int idx, const void *p) {
  StkId t;
  TValue k;
  t = index2addr(L, idx);
  api_check(L, ttistable(t), "table expected");
  setpvalue(&k, cast(void *, p));  // 轻量级用户数据
  setobj2s(L, L->top, vsH_get(hvalue(t), &k));
  api_incr_top(L);
  return ttnov(L->top - 1);
}


// 新建一个table对象放到栈顶
VS_API void vs_createtable (vs_State *L, int narray, int nrec) {
  Table *t;
  t = vsH_new(L);
  sethvalue(L, L->top, t);
  api_incr_top(L);
  if (narray > 0 || nrec > 0)
    vsH_resize(L, t, narray, nrec);
  vsC_checkGC(L);
}


// 将idx位置用户数据压入栈顶
VS_API int vs_getuservalue (vs_State *L, int idx) {
  StkId o;
  o = index2addr(L, idx);
  api_check(L, ttisfulluserdata(o), "full userdata expected");
  getuservalue(L, uvalue(o), L->top);
  api_incr_top(L);
  return ttnov(L->top - 1);
}


/*
** set functions (stack -> VS)
*/

/*
** t[k] = value at the top of the stack (where 'k' is a string)
*/
// 设置t[k]的值为当前栈顶的值 设置完成弹出栈顶
// aux auxiliary辅助
static void auxsetstr (vs_State *L, const TValue *t, const char *k) {
  const TValue *slot;
  TString *str = vsS_new(L, k);
  api_checknelems(L, 1);
  // 设置t[str]项为 L->top-1的值
  if (vsV_fastset(L, t, str, slot, vsH_getstr, L->top - 1))
    L->top--;  /* pop value */
  else {
    setsvalue2s(L, L->top, str);  /* push 'str' (to make it a TValue) 将要查询的key放到栈顶 */
    api_incr_top(L);
    // 设置t在键L->top-1上对应值为L->top-2上的值
    vsV_finishset(L, t, L->top - 1, L->top - 2, slot);
    L->top -= 2;  /* pop value and key 弹出两个值 */
  }
}


// 设置 _G[name] = 栈顶的值 弹出栈顶
VS_API void vs_setglobal (vs_State *L, const char *name) {
  Table *reg = hvalue(&G(L)->l_registry);
  auxsetstr(L, vsH_getint(reg, VS_RIDX_GLOBALS), name);
}


// t[top-2]=top-1 弹出 top-2和top-1
VS_API void vs_settable (vs_State *L, int idx) {
  StkId t;
  api_checknelems(L, 2);
  t = index2addr(L, idx);
  vsV_settable(L, t, L->top - 2, L->top - 1);
  L->top -= 2;  /* pop index and value */
}


// 设置idx位置k键的值为当前栈顶的值 设置完成弹出栈顶
// t[k]=top-1 弹出top-1
VS_API void vs_setfield (vs_State *L, int idx, const char *k) {
  auxsetstr(L, index2addr(L, idx), k);
}


// t[n]=top-1 弹出top-1
VS_API void vs_seti (vs_State *L, int idx, vs_Integer n) {
  StkId t;
  const TValue *slot;
  api_checknelems(L, 1);
  t = index2addr(L, idx);
  if (vsV_fastset(L, t, n, slot, vsH_getint, L->top - 1))
    L->top--;  /* pop value */
  else {
    setivalue(L->top, n);
    api_incr_top(L);
    vsV_finishset(L, t, L->top - 1, L->top - 2, slot);
    L->top -= 2;  /* pop value and key */
  }
}


// 设置idx位置k键的值为当前栈顶的值 弹出栈顶
VS_API void vs_setuservalue (vs_State *L, int idx) {
  StkId o;
  api_checknelems(L, 1);
  o = index2addr(L, idx);
  api_check(L, ttisfulluserdata(o), "full userdata expected");
  setuservalue(L, uvalue(o), L->top - 1);
  vsC_barrier(L, gcvalue(o), L->top - 1);
  L->top--;
}


/*
** 'load' and 'call' functions (run VS code)
*/

// 确保在na个参数和nr和返回值的情况下栈上空间还足够
// 返回值个数不确定或者栈上增加的参数个数(nr-na)不会溢出
#define checkresults(L,na,nr) \
     api_check(L, (nr) == VS_MULTRET || (L->ci->top - L->top >= (nr) - (na)), \
	"results from function overflow current stack size")


// 没有保护的调用
VS_API void vs_call (vs_State *L, int nargs, int nresults) {
  StkId func;
  api_checknelems(L, nargs+1);
  // 检查线程状态是否正常,只有处于正常状态下的线程才可以调用目标函数
  api_check(L, L->status == VS_OK, "cannot do calls on non-normal thread");
  checkresults(L, nargs, nresults);
  func = L->top - (nargs+1);
  vsD_call(L, func, nresults);  /* do the call */
  adjustresults(L, nresults);
}



/*
** Execute a protected call.
*/
struct CallS {  /* data to 'f_call' */
  StkId func;
  int nresults;
};


static void f_call (vs_State *L, void *ud) {
  struct CallS *c = cast(struct CallS *, ud);
  vsD_call(L, c->func, c->nresults);
}



// 有保护的调用
// 受保护方法的唯一区别:函数的调用都不会因为错误直接导致程序直接退出
// 而是退回到调用点,然后将状态返回到外层的逻辑处理
// errfunc传入0代表没有错误处理函数,docall函数中使用了msghandler作为了错误处理函数
VS_API int vs_pcall (vs_State *L, int nargs, int nresults, int errfunc) {
  struct CallS c;
  int status;
  ptrdiff_t func;
  api_checknelems(L, nargs+1);
  api_check(L, L->status == VS_OK, "cannot do calls on non-normal thread");
  checkresults(L, nargs, nresults);
  if (errfunc == 0)
    func = 0;
  else {
    StkId o = index2addr(L, errfunc);
    api_checkstackindex(L, errfunc, o);
    func = savestack(L, o);
  }
  // 获取真正被调用的函数,第一个参数前面就是被调用的函数
  c.func = L->top - (nargs+1);  /* function to be called */
  c.nresults = nresults;  /* do a 'conventional' protected call */
  status = vsD_pcall(L, f_call, &c, savestack(L, c.func), func);
  adjustresults(L, nresults);
  return status;
}


VS_API int vs_load (vs_State *L, vs_Reader reader, void *data,
                      const char *chunkname, const char *mode) {
  ZIO z;
  int status;
  if (!chunkname) chunkname = "?";
  // 初始化刚才定义的z
  vsZ_init(L, &z, reader, data);
  // 调用后栈顶就是识别的函数闭包
  status = vsD_protectedparser(L, &z, chunkname, mode);
  if (status == VS_OK) {  /* no errors? */
    LClosure *f = clLvalue(L->top - 1);  /* get newly created function */
    // 刚识别完的vs闭包的upvalue还是nil,在这里初始化为_G变量
    if (f->nupvalues >= 1) {  /* does it have an upvalue? */
      /* get global table from registry */
      Table *reg = hvalue(&G(L)->l_registry);
      const TValue *gt = vsH_getint(reg, VS_RIDX_GLOBALS);
      /* set global table as 1st upvalue of 'f' (may be VS_ENV) */
      setobj(L, f->upvals[0]->v, gt);
      vsC_upvalbarrier(L, f->upvals[0]);
    }
  }
  return status;
}


VS_API int vs_dump (vs_State *L, vs_Writer writer, void *data, int strip) {
  int status;
  TValue *o;
  api_checknelems(L, 1);
  o = L->top - 1;
  if (isLfunction(o))
    status = vsU_dump(L, getproto(o), writer, data, strip);
  else
    status = 1;
  return status;
}


/*
** miscellaneous functions
*/


VS_API int vs_error (vs_State *L) {
  api_checknelems(L, 1);
  vsG_errormsg(L);
  return 0;  /* to avoid warnings */
}


VS_API int vs_next (vs_State *L, int idx) {
  StkId t;
  int more;
  t = index2addr(L, idx);
  api_check(L, ttistable(t), "table expected");
  more = vsH_next(L, hvalue(t), L->top - 1);
  if (more) {
    api_incr_top(L);
  }
  else  /* no more elements */
    L->top -= 1;  /* remove key */
  return more;
}


VS_API void vs_concat (vs_State *L, int n) {
  api_checknelems(L, n);
  if (n >= 2) {
    vsV_concat(L, n);
  }
  else if (n == 0) {  /* push empty string */
    setsvalue2s(L, L->top, vsS_newlstr(L, "", 0));
    api_incr_top(L);
  }
  /* else n == 1; nothing to do */
  vsC_checkGC(L);
}


// 将对象的长度压入栈顶
VS_API void vs_len (vs_State *L, int idx) {
  StkId t;
  t = index2addr(L, idx);
  vsV_objlen(L, L->top, t);
  api_incr_top(L);
}


VS_API vs_Alloc vs_getallocf (vs_State *L, void **ud) {
  vs_Alloc f;
  if (ud) *ud = G(L)->ud;
  f = G(L)->frealloc;
  return f;
}


VS_API void vs_setallocf (vs_State *L, vs_Alloc f, void *ud) {
  G(L)->ud = ud;
  G(L)->frealloc = f;
}


// 创建一个userdata压入栈顶 返回userdata的数据实际存放位置
VS_API void *vs_newuserdata (vs_State *L, size_t size) {
  Udata *u;
  u = vsS_newudata(L, size);
  setuvalue(L, L->top, u);
  api_incr_top(L);
  vsC_checkGC(L);
  return getudatamem(u);
}

static const char *aux_upvalue (StkId fi, int n, TValue **val,
                                CClosure **owner, UpVal **uv) {
  switch (ttype(fi)) {
    case VS_TCCL: {  /* C closure */
      CClosure *f = clCvalue(fi);
      if (!(1 <= n && n <= f->nupvalues)) return NULL;
      *val = &f->upvalue[n-1];
      if (owner) *owner = f;
      return "";
    }
    case VS_TLCL: {  /* VS closure */
      LClosure *f = clLvalue(fi);
      TString *name;
      Proto *p = f->p;
      if (!(1 <= n && n <= p->sizeupvalues)) return NULL;
      *val = f->upvals[n-1]->v;
      if (uv) *uv = f->upvals[n - 1];
      name = p->upvalues[n-1].name;
      return (name == NULL) ? "(*no name)" : getstr(name);
    }
    default: return NULL;  /* not a closure */
  }
}

VS_API const char *vs_getupvalue (vs_State *L, int funcindex, int n) {
  const char *name;
  TValue *val = NULL;  /* to avoid warnings */
  name = aux_upvalue(index2addr(L, funcindex), n, &val, NULL, NULL);
  if (name) {
    setobj2s(L, L->top, val);
    api_incr_top(L);
  }
  return name;
}


VS_API const char *vs_setupvalue (vs_State *L, int funcindex, int n) {
  const char *name;
  TValue *val = NULL;  /* to avoid warnings */
  CClosure *owner = NULL;
  UpVal *uv = NULL;
  StkId fi;
  fi = index2addr(L, funcindex);
  api_checknelems(L, 1);
  name = aux_upvalue(fi, n, &val, &owner, &uv);
  if (name) {
    L->top--;
    setobj(L, val, L->top);
    if (owner) { vsC_barrier(L, owner, L->top); }
    else if (uv) { vsC_upvalbarrier(L, uv); }
  }
  return name;
}
