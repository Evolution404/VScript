#define lvm_c
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vs.h"

#include "vdebug.h"
#include "vdo.h"
#include "vfunc.h"
#include "vgc.h"
#include "vobject.h"
#include "vopcodes.h"
#include "vstate.h"
#include "vstring.h"
#include "vtable.h"
#include "vvm.h"

// 负责从整数和字符串转换成浮点数 转换成功返回1 失败返回0
int vsV_tonumber_ (const TValue *obj, vs_Number *n) {
  TValue v;
  if (ttisinteger(obj)) {  // 整数类型直接转换
    *n = cast_num(ivalue(obj));
    return 1;
  }
  else if (cvt2num(obj) &&  /* string convertible to number? 字符串是否可以转换成数字 */
            // vsO_str2num返回的是转换的字符串大小包括\0 vslen返回的是字符串长度不包括\0
            vsO_str2num(svalue(obj), &v) == vslen(obj) + 1) {
    *n = nvalue(&v);  /* convert result of 'vsO_str2num' to a float */
    return 1;
  }
  else
    return 0;  /* conversion failed */
}


/*
** try to convert a value to an integer, rounding according to 'mode':
** mode == 0: accepts only integral values  只接受整数也包括字符串类型的整数
** mode == 1: takes the floor of the number 将浮点数向下取整
** mode == 2: takes the ceil of the number  将浮点数向上取整
*/
// 返回值 0转换失败 1转换成功
int vsV_tointeger (const TValue *obj, vs_Integer *p, int mode) {
  TValue v;
 again:
  if (ttisfloat(obj)) {  // 传入了浮点数
    vs_Number n = fltvalue(obj);
    vs_Number f = l_floor(n);  // 求出floor后的值
    if (n != f) {  /* not an integral value? */
      if (mode == 0) return 0;  /* fails if mode demands integral value */
      else if (mode > 1)  /* needs ceil? */
        f += 1;  /* convert floor to ceil (remember: n != f) mode是2代表取ceil也就是floor+1 */
    }
    // 从double类型转换成long long类型, 转换过程还进行了范围判断
    return vs_numbertointeger(f, p);
  }
  else if (ttisinteger(obj)) {  // 传入了整数
    *p = ivalue(obj);
    return 1;
  }
  // 判断字符串能转换成浮点数
  else if (cvt2num(obj) &&
            vsO_str2num(svalue(obj), &v) == vslen(obj) + 1) {
    // 先尝试转换成数字 跳转回again处再使用整数浮点数的处理方法
    obj = &v;
    goto again;  /* convert result from 'vsO_str2num' to an integer */
  }
  return 0;  /* conversion failed */
}


// 用于执行for循环
static int forlimit (const TValue *obj, vs_Integer *p, vs_Integer step,
                     int *stopnow) {
  *stopnow = 0;  /* usually, let loops run */
  if (!vsV_tointeger(obj, p, (step < 0 ? 2 : 1))) {  /* not fit in integer? */
    vs_Number n;  /* try to convert to float */
    if (!tonumber(obj, &n)) /* cannot convert to float? */
      return 0;  /* not a number */
    if (vsi_numlt(0, n)) {  /* if true, float is larger than max integer */
      *p = VS_MAXINTEGER;
      if (step < 0) *stopnow = 1;
    }
    else {  /* float is smaller than min integer */
      *p = VS_MININTEGER;
      if (step >= 0) *stopnow = 1;
    }
  }
  return 1;
}


// t[key] = val
// slot的意义与上面函数一致
// 查询__newindex元方法, 如果是
void vsV_finishset (vs_State *L, const TValue *t, TValue *key,
                     StkId val, const TValue *slot) {
  if (slot == NULL)
    vsG_typeerror(L, t, "index");
  else {
    Table *h = hvalue(t);  /* save 't' table */
    vs_assert(ttisnil(slot));  /* old value must be nil */
    if (slot == vsO_nilobject)  // 是nilobject说明key还没创建过
      slot = vsH_newkey(L, h, key);  /* create one 返回的新Node的val字段 */
    setobj2t(L, cast(TValue *, slot), val);  /* set its new value 直接往新Node上赋值 */
    vsC_barrierback(L, h, val);
  }
}

// 用于比较两个TString对象的大小
static int l_strcmp (const TString *ls, const TString *rs) {
  const char *l = getstr(ls);
  size_t ll = tsslen(ls);
  const char *r = getstr(rs);
  size_t lr = tsslen(rs);
  for (;;) {  /* for each segment */
    int temp = strcoll(l, r);
    if (temp != 0)  /* not equal? */
      return temp;  /* done */
    else {  /* strings are equal up to a '\0' */
      size_t len = strlen(l);  /* index of first '\0' in both strings */
      if (len == lr)  /* 'rs' is finished? */
        return (len == ll) ? 0 : 1;  /* check 'ls' */
      else if (len == ll)  /* 'ls' is finished? */
        return -1;  /* 'ls' is smaller than 'rs' ('rs' is not finished) */
      /* both strings longer than 'len'; go on comparing after the '\0' */
      len++;
      l += len; ll -= len; r += len; lr -= len;
    }
  }
}

// 以下四个函数用于比较数字的大小,LT是小于,LE是小于等于

static int LTintfloat (vs_Integer i, vs_Number f) {
  return vsi_numlt(cast_num(i), f);  /* compare them as floats */
}


/*
** Check whether integer 'i' is less than or equal to float 'f'.
** See comments on previous function.
*/
static int LEintfloat (vs_Integer i, vs_Number f) {
  return vsi_numle(cast_num(i), f);  /* compare them as floats */
}


/*
** Return 'l < r', for numbers.
*/
static int LTnum (const TValue *l, const TValue *r) {
  if (ttisinteger(l)) {
    vs_Integer li = ivalue(l);
    if (ttisinteger(r))
      return li < ivalue(r);  /* both are integers */
    else  /* 'l' is int and 'r' is float */
      return LTintfloat(li, fltvalue(r));  /* l < r ? */
  }
  else {
    vs_Number lf = fltvalue(l);  /* 'l' must be float */
    if (ttisfloat(r))
      return vsi_numlt(lf, fltvalue(r));  /* both are float */
    else if (vsi_numisnan(lf))  /* 'r' is int and 'l' is float */
      return 0;  /* NaN < i is always false */
    else  /* without NaN, (l < r)  <-->  not(r <= l) */
      return !LEintfloat(ivalue(r), lf);  /* not (r <= l) ? */
  }
}


/*
** Return 'l <= r', for numbers.
*/
static int LEnum (const TValue *l, const TValue *r) {
  if (ttisinteger(l)) {
    vs_Integer li = ivalue(l);
    if (ttisinteger(r))
      return li <= ivalue(r);  /* both are integers */
    else  /* 'l' is int and 'r' is float */
      return LEintfloat(li, fltvalue(r));  /* l <= r ? */
  }
  else {
    vs_Number lf = fltvalue(l);  /* 'l' must be float */
    if (ttisfloat(r))
      return vsi_numle(lf, fltvalue(r));  /* both are float */
    else if (vsi_numisnan(lf))  /* 'r' is int and 'l' is float */
      return 0;  /*  NaN <= i is always false */
    else  /* without NaN, (l <= r)  <-->  not(r < l) */
      return !LTintfloat(ivalue(r), lf);  /* not (r < l) ? */
  }
}

/*
** Main operation less than; return 'l < r'.
*/
// 都是数字,或者都是字符串就可以进行计算'l < r'
int vsV_lessthan (vs_State *L, const TValue *l, const TValue *r) {
  if (ttisnumber(l) && ttisnumber(r))  /* both operands are numbers? */
    return LTnum(l, r);
  else if (ttisstring(l) && ttisstring(r))  /* both are strings? */
    return l_strcmp(tsvalue(l), tsvalue(r)) < 0;
  vsG_ordererror(L, l, r);  /* error */
  return -1;
}

int vsV_lessequal (vs_State *L, const TValue *l, const TValue *r) {
  if (ttisnumber(l) && ttisnumber(r))  /* both operands are numbers? */
    return LEnum(l, r);
  else if (ttisstring(l) && ttisstring(r))  /* both are strings? */
    return l_strcmp(tsvalue(l), tsvalue(r)) <= 0;
  vsG_ordererror(L, l, r);  /* error */
  return -1;
}

// 比较t1和t2是否相等
int vsV_equalobj (const TValue *t1, const TValue *t2) {
  if (ttype(t1) != ttype(t2)) {  /* not the same variant? 两个数据不是完全一样的类型 */
    // 四种可能 1:t1t2是数字 2:t1是数字t2不是 3:t1不是数字t2是数字 4:t1t2不是数字
    // 2和3主类型不同 4则t1不是数字 所以只剩1 t1t2都是数字
    if (ttnov(t1) != ttnov(t2) || ttnov(t1) != VS_TNUMBER)  // 两者主类型不一样 或 t1不是数字类型
      return 0;  /* only numbers can be equal with different variants */
    else {  /* two numbers with different variants */
      vs_Integer i1, i2;  /* compare them as integers */
      return (tointeger(t1, &i1) && tointeger(t2, &i2) && i1 == i2);
    }
  }
  /* values have same type and same variant */
  // t1和t2的类型完全一致
  switch (ttype(t1)) {
    case VS_TNIL: return 1;
    case VS_TNUMINT: return (ivalue(t1) == ivalue(t2));
    case VS_TNUMFLT: return vsi_numeq(fltvalue(t1), fltvalue(t2));
    case VS_TBOOLEAN: return bvalue(t1) == bvalue(t2);  /* true must be 1 !! */
    case VS_TLIGHTUSERDATA: return pvalue(t1) == pvalue(t2);
    case VS_TLCF: return fvalue(t1) == fvalue(t2);
    case VS_TSHRSTR: return eqshrstr(tsvalue(t1), tsvalue(t2));
    case VS_TLNGSTR: return vsS_eqlngstr(tsvalue(t1), tsvalue(t2));
    case VS_TUSERDATA: return uvalue(t1) == uvalue(t2);
    case VS_TTABLE: return hvalue(t1) == hvalue(t2);
    default:
      return gcvalue(t1) == gcvalue(t2);
  }
}

// 确保o一定是一个字符串 如果不是字符串也会转换成字符串
// 这个宏用于vsV_concat函数
#define tostring(L,o)  \
	(ttisstring(o) || (cvt2str(o) && (vsO_tostring(L, o), 1)))

// 判断是不是空字符串 空字符串一定是短字符串,然后短字符串的长度为0
#define isemptystr(o)	(ttisshrstring(o) && tsvalue(o)->shrlen == 0)

// 拷贝 top-1 top-2 ... top-n位置的字符串到buff里
static void copy2buff (StkId top, int n, char *buff) {
  size_t tl = 0;  /* size already copied */
  do {
    size_t l = vslen(top - n);  /* length of string being copied */
    memcpy(buff + tl, svalue(top - n), l * sizeof(char));
    tl += l;
  } while (--n > 0);
}


/*
** Main operation for concatenation: concat 'total' values in the stack,
** from 'L->top - total' up to 'L->top - 1'.
*/
// 将 top-1 top-2 ... top-total上的字符串进行合并
// 执行完毕新的top-1就放着合并后的结果
void vsV_concat (vs_State *L, int total) {
  vs_assert(total >= 2);
  do {
    StkId top = L->top;
    int n = 2;  /* number of elements handled in this pass (at least 2) */
    // 如果top-1或者top-2位置的元素有一个是 不是字符串还不能转换成字符串的类型就报错
    if (!(ttisstring(top-2) || cvt2str(top-2)) || !tostring(L, top-1))
      vsG_concaterror(L, top-2, top-1);
    // 运算结果放在 top-2 的位置
    else if (isemptystr(top - 1))  /* second operand is empty? */
      cast_void(tostring(L, top - 2));  /* result is first operand top-1是空结果就是top-2 */
    else if (isemptystr(top - 2)) {  /* first operand is an empty string? */
      setobjs2s(L, top - 2, top - 1);  /* result is second op. top-2是空结果是top-1 拷贝到top-2位置 */
    }
    else {
      // 连接两个字符串类型
      /* at least two non-empty string values; get as many as possible */
      size_t tl = vslen(top - 1);  // tl最开始等于top-1字符串的长度
      TString *ts;
      /* collect total length and number of strings */
      // 从top-1在栈上不断向前搜索 计算合并所有字符串后的长度tl(total len)
      for (n = 1; n < total && tostring(L, top - n - 1); n++) {
        size_t l = vslen(top - n - 1);
        if (l >= (MAX_SIZE/sizeof(char)) - tl)
          vsG_runerror(L, "string length overflow");
        tl += l;
      }
      if (tl <= VSI_MAXSHORTLEN) {  /* is result a short string? 这是一个短字符串 */
        char buff[VSI_MAXSHORTLEN];
        copy2buff(top, n, buff);  /* copy strings to buffer 把这几个字符串拷贝到buff里 */
        ts = vsS_newlstr(L, buff, tl);  // 创建新字符串
      }
      else {  /* long string; copy strings directly to final result 长字符串 */
        // 为什么不使用上面短字符串的创建buff再拷贝到buff里 使用vsS_newlstr创建字符串?
        // 这里如果使用 char buff[tl] 然后再拷贝进去会有可能造成栈溢出
        // 如果使用malloc再free就不如使用下面的方式了
        ts = vsS_createlngstrobj(L, tl);  // 创建字符串对象 申请字符串的空间
        copy2buff(top, n, getstr(ts));     // 把实际内容拷贝进去
      }
      setsvalue2s(L, top - n, ts);  /* create result */
    }
    total -= n-1;  /* got 'n' strings to create 1 new 这一轮已经合并了n-1个了 */
    L->top -= n-1;  /* popped 'n' strings and pushed one top要向前移动n-1个 */
  } while (total > 1);  /* repeat until only 1 result left */
}

// 求对象的长度,即计算len(obj)
void vsV_objlen (vs_State *L, StkId ra, const TValue *rb) {
  switch (ttype(rb)) {
    case VS_TTABLE: {
      Table *h = hvalue(rb);
      setivalue(ra, vsH_getn(h));  /* else primitive len */
      return;
    }
    case VS_TSHRSTR: {
      setivalue(ra, tsvalue(rb)->shrlen);
      return;
    }
    case VS_TLNGSTR: {
      setivalue(ra, tsvalue(rb)->u.lnglen);
      return;
    }
    default: {  /* try metamethod */
      vsG_typeerror(L, rb, "get length of");
      break;
    }
  }
}

/*
** Integer division; return 'm // n', that is, floor(m/n).
** C division truncates its result (rounds towards zero).
** 'floor(q) == trunc(q)' when 'q >= 0' or when 'q' is integer,
** otherwise 'floor(q) == trunc(q) - 1'.
*/
vs_Integer vsV_div (vs_State *L, vs_Integer m, vs_Integer n) {
  if (l_castS2U(n) + 1u <= 1u) {  /* special cases: -1 or 0 */
    if (n == 0)
      vsG_runerror(L, "attempt to divide by zero");
    return intop(-, 0, m);   /* n==-1; avoid overflow with 0x80000...//-1 */
  }
  else {
    vs_Integer q = m / n;  /* perform C division */
    if ((m ^ n) < 0 && m % n != 0)  /* 'm/n' would be negative non-integer? */
      q -= 1;  /* correct result for different rounding */
    return q;
  }
}

/*
** Integer modulus; return 'm % n'. (Assume that C '%' with
** negative operands follows C99 behavior. See previous comment
** about vsV_div.)
*/
vs_Integer vsV_mod (vs_State *L, vs_Integer m, vs_Integer n) {
  if (l_castS2U(n) + 1u <= 1u) {  /* special cases: -1 or 0 */
    if (n == 0)
      vsG_runerror(L, "attempt to perform 'n%%0'");
    return 0;   /* m % -1 == 0; avoid overflow with 0x80000...%-1 */
  }
  else {
    vs_Integer r = m % n;
    if (r != 0 && (m ^ n) < 0)  /* 'm/n' would be non-integer negative? */
      r += n;  /* correct result for different rounding */
    return r;
  }
}


/* number of bits in an integer */
#define NBITS	cast_int(sizeof(vs_Integer) * CHAR_BIT)

/*
** Shift left operation. (Shift right just negates 'y'.)
*/
vs_Integer vsV_shiftl (vs_Integer x, vs_Integer y) {
  if (y < 0) {  /* shift right? */
    if (y <= -NBITS) return 0;
    else return intop(>>, x, -y);
  }
  else {  /* shift left */
    if (y >= NBITS) return 0;
    else return intop(<<, x, y);
  }
}

/*
** check whether cached closure in prototype 'p' may be reused, that is,
** whether there is a cached closure with the same upvalues needed by
** new closure to be created.
*/
static LClosure *getcached (Proto *p, UpVal **encup, StkId base) {
  LClosure *c = p->cache;
  // 如果有缓存的闭包 检查这个缓存的闭包的upvalue是否与当前需要的完全一致
  if (c != NULL) {  /* is there a cached closure? */
    int nup = p->sizeupvalues;
    Upvaldesc *uv = p->upvalues;
    int i;
    for (i = 0; i < nup; i++) {  /* check whether it has right upvalues */
      TValue *v = uv[i].instack ? base + uv[i].idx : encup[uv[i].idx]->v;
      if (c->upvals[i]->v != v)
        return NULL;  /* wrong upvalue; cannot reuse closure */
    }
  }
  return c;  /* return cached closure (or NULL if no cached closure) */
}


/*
** create a new VS closure, push it in the stack, and initialize
** its upvalues. Note that the closure is not cached if prototype is
** already black (which means that 'cache' was already cleared by the
** GC).
*/
static void pushclosure (vs_State *L, Proto *p, UpVal **encup, StkId base,
                         StkId ra) {
  int nup = p->sizeupvalues;
  Upvaldesc *uv = p->upvalues;
  int i;
  // 创建一个新的vs函数闭包,写入OP_CLOSURE的参数A位置
  LClosure *ncl = vsF_newLclosure(L, nup);
  // 新建的闭包的与传入的原型联系起来
  ncl->p = p;
  setclLvalue(L, ra, ncl);  /* anchor new closure in stack */
  // 为新建的闭包写入upvalue信息
  for (i = 0; i < nup; i++) {  /* fill in its upvalues */
    if (uv[i].instack)  /* upvalue refers to local variable? */
      ncl->upvals[i] = vsF_findupval(L, base + uv[i].idx);
    else  /* get upvalue from enclosing function */
      ncl->upvals[i] = encup[uv[i].idx];
    ncl->upvals[i]->refcount++;
    /* new closure is white, so we do not need a barrier here */
  }
  // 在原型上缓存新建的闭包,getcached函数复用
  // 如果继续调用这个函数就不用重新创建了
  if (!isblack(p))  /* cache will not break GC invariant? */
    p->cache = ncl;  /* save it on cache for reuse */
}


/*
** {==================================================================
** Function 'vsV_execute': main interpreter loop
** ===================================================================
*/


/*
** some macros for common tasks in 'vsV_execute'
*/


#define RA(i)	(base+GETARG_A(i))
#define RB(i)	check_exp(getBMode(GET_OPCODE(i)) == OpArgR, base+GETARG_B(i))
#define RC(i)	check_exp(getCMode(GET_OPCODE(i)) == OpArgR, base+GETARG_C(i))
#define RKB(i)	check_exp(getBMode(GET_OPCODE(i)) == OpArgK, \
	ISK(GETARG_B(i)) ? k+INDEXK(GETARG_B(i)) : base+GETARG_B(i))
#define RKC(i)	check_exp(getCMode(GET_OPCODE(i)) == OpArgK, \
	ISK(GETARG_C(i)) ? k+INDEXK(GETARG_C(i)) : base+GETARG_C(i))


/* execute a jump instruction */
#define dojump(ci,i,e) \
  { int a = GETARG_A(i); \
    if (a != 0) vsF_close(L, ci->base + a - 1); \
    ci->savedpc += GETARG_sBx(i) + e; }

/* for test instructions, execute the jump instruction that follows it */
#define donextjump(ci)	{ i = *ci->savedpc; dojump(ci, i, 1); }


#define Protect(x)	{ {x;}; base = ci->base; }

#define checkGC(L,c)  \
	{ vsC_condGC(L, L->top = (c),  /* limit of live values */ \
                         Protect(L->top = ci->top));}  /* restore top */


/* fetch an instruction and prepare its execution */
#define vmfetch()	{ \
  i = *(ci->savedpc++); \
  ra = RA(i); /* WARNING: any stack reallocation invalidates 'ra' */ \
  vs_assert(base == ci->base); \
  vs_assert(base <= L->top && L->top < L->stack + L->stacksize); \
}

#define vmdispatch(o)	switch(o)
#define vmcase(l)	case l:
#define vmbreak		break


/*
** copy of 'vsV_gettable', but protecting the call to potential
** metamethod (which can reallocate the stack)
*/
// 传入表t和键k,将查询结果写入v
#define gettableProtected(L,t,k,v)  { const TValue *slot; \
  if (vsV_fastget(L,t,k,slot,vsH_get)) { setobj2s(L, v, slot); } \
  else vsG_typeerror(L, t, "index"); }


/* same for 'vsV_settable' */
#define settableProtected(L,t,k,v) { const TValue *slot; \
  if (!vsV_fastset(L,t,k,slot,vsH_get,v)) \
    Protect(vsV_finishset(L,t,k,v,slot)); }



void vsV_execute (vs_State *L) {
  CallInfo *ci = L->ci;
  LClosure *cl;
  TValue *k;
  StkId base;
  // 进入vsV_execute函数的CallInfo被标记CIST_FRESH
  // 用来识别当前CallInfo是不是进入时的那一个
  ci->callstatus |= CIST_FRESH;  /* fresh invocation of 'vsV_execute" */
 newframe:  /* reentry point when frame changes (call/return) */
  vs_assert(ci == L->ci);
  cl = clLvalue(ci->func);  /* local reference to function's closure */
  k = cl->p->k;  /* local reference to function's constant table */
  base = ci->base;  /* local copy of function's base */
  /* main loop of interpreter */
  for (;;) {
    Instruction i;
    StkId ra;
    // 获取当前指令i和参数A代表的寄存器位置ra,让ci->u.l.savedpc自增
    vmfetch();
    vmdispatch (GET_OPCODE(i)) {
      vmcase(OP_MOVE) {
        setobjs2s(L, ra, RB(i));
        vmbreak;
      }
      vmcase(OP_LOADK) {
        TValue *rb = k + GETARG_Bx(i);
        setobj2s(L, ra, rb);
        vmbreak;
      }
      // OP_LOADKX还需要一条OP_EXTRAARG指令进行配合
      vmcase(OP_LOADKX) {
        TValue *rb;
        vs_assert(GET_OPCODE(*ci->savedpc) == OP_EXTRAARG);
        rb = k + GETARG_Ax(*ci->savedpc++);
        setobj2s(L, ra, rb);
        vmbreak;
      }
      vmcase(OP_LOADBOOL) {
        setbvalue(ra, GETARG_B(i));
        if (GETARG_C(i)) ci->savedpc++;  /* skip next instruction (if C) */
        vmbreak;
      }
      vmcase(OP_LOADNIL) {
        // 总共需要设置b+1次nil
        int b = GETARG_B(i);
        do {
          setnilvalue(ra++);
        } while (b--);
        vmbreak;
      }
      vmcase(OP_GETUPVAL) {
        int b = GETARG_B(i);
        setobj2s(L, ra, cl->upvals[b]->v);
        vmbreak;
      }
      vmcase(OP_GETTABUP) {
        TValue *upval = cl->upvals[GETARG_B(i)]->v;
        TValue *rc = RKC(i);
        gettableProtected(L, upval, rc, ra);
        vmbreak;
      }
      vmcase(OP_GETTABLE) {
        StkId rb = RB(i);
        TValue *rc = RKC(i);
        gettableProtected(L, rb, rc, ra);
        vmbreak;
      }
      vmcase(OP_SETTABUP) {
        TValue *upval = cl->upvals[GETARG_A(i)]->v;
        TValue *rb = RKB(i);
        TValue *rc = RKC(i);
        settableProtected(L, upval, rb, rc);
        vmbreak;
      }
      vmcase(OP_SETUPVAL) {
        UpVal *uv = cl->upvals[GETARG_B(i)];
        setobj(L, uv->v, ra);
        vsC_upvalbarrier(L, uv);
        vmbreak;
      }
      vmcase(OP_SETTABLE) {
        TValue *rb = RKB(i);
        TValue *rc = RKC(i);
        settableProtected(L, ra, rb, rc);
        vmbreak;
      }
      vmcase(OP_NEWTABLE) {
        int b = GETARG_B(i);
        int c = GETARG_C(i);
        Table *t = vsH_new(L);
        sethvalue(L, ra, t);
        if (b != 0 || c != 0)
          vsH_resize(L, t, vsO_fb2int(b), vsO_fb2int(c));
        checkGC(L, ra + 1);
        vmbreak;
      }
      vmcase(OP_SELF) {
        // 让B位置赋值到A
        // 查询B.C放到A+1位置
        const TValue *aux;
        StkId rb = RB(i);
        TValue *rc = RKC(i);
        TString *key = tsvalue(rc);  /* key must be a string */
        setobjs2s(L, ra + 1, rb);
        if (vsV_fastget(L, rb, key, aux, vsH_getstr)) {
          setobj2s(L, ra, aux);
        }
        else Protect(vsG_typeerror(L, rb, "index"););
        vmbreak;
      }
      vmcase(OP_ADD) {
        TValue *rb = RKB(i);
        TValue *rc = RKC(i);
        vs_Number nb; vs_Number nc;
        if (ttisinteger(rb) && ttisinteger(rc)) {
          vs_Integer ib = ivalue(rb); vs_Integer ic = ivalue(rc);
          setivalue(ra, intop(+, ib, ic));
        }
        else if (tonumber(rb, &nb) && tonumber(rc, &nc)) {
          setfltvalue(ra, vsi_numadd(L, nb, nc));
        }
        else
          vsG_opinterror(L, rb, rc, "perform arithmetic on");
        vmbreak;
      }
      vmcase(OP_SUB) {
        TValue *rb = RKB(i);
        TValue *rc = RKC(i);
        vs_Number nb; vs_Number nc;
        if (ttisinteger(rb) && ttisinteger(rc)) {
          vs_Integer ib = ivalue(rb); vs_Integer ic = ivalue(rc);
          setivalue(ra, intop(-, ib, ic));
        }
        else if (tonumber(rb, &nb) && tonumber(rc, &nc)) {
          setfltvalue(ra, vsi_numsub(L, nb, nc));
        }
        else
          vsG_opinterror(L, rb, rc, "perform arithmetic on");
        vmbreak;
      }
      vmcase(OP_MUL) {
        TValue *rb = RKB(i);
        TValue *rc = RKC(i);
        vs_Number nb; vs_Number nc;
        if (ttisinteger(rb) && ttisinteger(rc)) {
          vs_Integer ib = ivalue(rb); vs_Integer ic = ivalue(rc);
          setivalue(ra, intop(*, ib, ic));
        }
        else if (tonumber(rb, &nb) && tonumber(rc, &nc)) {
          setfltvalue(ra, vsi_nummul(L, nb, nc));
        }
        else
          vsG_opinterror(L, rb, rc, "perform arithmetic on");
        vmbreak;
      }
      vmcase(OP_DIV) {  /* float division (always with floats) */
        TValue *rb = RKB(i);
        TValue *rc = RKC(i);
        vs_Number nb; vs_Number nc;
        if (tonumber(rb, &nb) && tonumber(rc, &nc)) {
          setfltvalue(ra, vsi_numdiv(L, nb, nc));
        }
        else
          vsG_opinterror(L, rb, rc, "perform arithmetic on");
        vmbreak;
      }
      vmcase(OP_BAND) {
        TValue *rb = RKB(i);
        TValue *rc = RKC(i);
        vs_Integer ib; vs_Integer ic;
        if (tointeger(rb, &ib) && tointeger(rc, &ic)) {
          setivalue(ra, intop(&, ib, ic));
        }
        else
          vsG_opinterror(L, rb, rc, "perform bitwise operation on");
        vmbreak;
      }
      vmcase(OP_BOR) {
        TValue *rb = RKB(i);
        TValue *rc = RKC(i);
        vs_Integer ib; vs_Integer ic;
        if (tointeger(rb, &ib) && tointeger(rc, &ic)) {
          setivalue(ra, intop(|, ib, ic));
        }
        else
          vsG_opinterror(L, rb, rc, "perform bitwise operation on");
        vmbreak;
      }
      vmcase(OP_BXOR) {
        TValue *rb = RKB(i);
        TValue *rc = RKC(i);
        vs_Integer ib; vs_Integer ic;
        if (tointeger(rb, &ib) && tointeger(rc, &ic)) {
          setivalue(ra, intop(^, ib, ic));
        }
        else
          vsG_opinterror(L, rb, rc, "perform bitwise operation on");
        vmbreak;
      }
      vmcase(OP_SHL) {
        TValue *rb = RKB(i);
        TValue *rc = RKC(i);
        vs_Integer ib; vs_Integer ic;
        if (tointeger(rb, &ib) && tointeger(rc, &ic)) {
          setivalue(ra, vsV_shiftl(ib, ic));
        }
        else
          vsG_opinterror(L, rb, rc, "perform bitwise operation on");
        vmbreak;
      }
      vmcase(OP_SHR) {
        TValue *rb = RKB(i);
        TValue *rc = RKC(i);
        vs_Integer ib; vs_Integer ic;
        if (tointeger(rb, &ib) && tointeger(rc, &ic)) {
          setivalue(ra, vsV_shiftl(ib, -ic));
        }
        else
          vsG_opinterror(L, rb, rc, "perform bitwise operation on");
        vmbreak;
      }
      vmcase(OP_MOD) {
        TValue *rb = RKB(i);
        TValue *rc = RKC(i);
        vs_Number nb; vs_Number nc;
        if (ttisinteger(rb) && ttisinteger(rc)) {
          vs_Integer ib = ivalue(rb); vs_Integer ic = ivalue(rc);
          setivalue(ra, vsV_mod(L, ib, ic));
        }
        else if (tonumber(rb, &nb) && tonumber(rc, &nc)) {
          vs_Number m;
          vsi_nummod(L, nb, nc, m);
          setfltvalue(ra, m);
        }
        else
          vsG_opinterror(L, rb, rc, "perform arithmetic on");
        vmbreak;
      }
      vmcase(OP_IDIV) {  /* floor division */
        TValue *rb = RKB(i);
        TValue *rc = RKC(i);
        vs_Number nb; vs_Number nc;
        if (ttisinteger(rb) && ttisinteger(rc)) {
          vs_Integer ib = ivalue(rb); vs_Integer ic = ivalue(rc);
          setivalue(ra, vsV_div(L, ib, ic));
        }
        else if (tonumber(rb, &nb) && tonumber(rc, &nc)) {
          setfltvalue(ra, vsi_numidiv(L, nb, nc));
        }
        else
          vsG_opinterror(L, rb, rc, "perform arithmetic on");
        vmbreak;
      }
      vmcase(OP_POW) {
        TValue *rb = RKB(i);
        TValue *rc = RKC(i);
        vs_Number nb; vs_Number nc;
        if (tonumber(rb, &nb) && tonumber(rc, &nc)) {
          setfltvalue(ra, vsi_numpow(L, nb, nc));
        }
        else
          vsG_opinterror(L, rb, rc, "perform arithmetic on");
        vmbreak;
      }
      vmcase(OP_UNM) {
        TValue *rb = RB(i);
        vs_Number nb;
        if (ttisinteger(rb)) {
          vs_Integer ib = ivalue(rb);
          setivalue(ra, intop(-, 0, ib));
        }
        else if (tonumber(rb, &nb)) {
          setfltvalue(ra, vsi_numunm(L, nb));
        }
        else
          vsG_opinterror(L, rb, rb, "perform arithmetic on");
        vmbreak;
      }
      vmcase(OP_BNOT) {
        TValue *rb = RB(i);
        vs_Integer ib;
        if (tointeger(rb, &ib)) {
          setivalue(ra, intop(^, ~l_castS2U(0), ib));
        }
        else
          vsG_opinterror(L, rb, rb, "perform bitwise operation on");
        vmbreak;
      }
      vmcase(OP_NOT) {
        TValue *rb = RB(i);
        int res = l_isfalse(rb);  /* next assignment may change this value */
        setbvalue(ra, res);
        vmbreak;
      }
      vmcase(OP_LEN) {
        Protect(vsV_objlen(L, ra, RB(i)));
        vmbreak;
      }
      vmcase(OP_CONCAT) {
        int b = GETARG_B(i);
        int c = GETARG_C(i);
        StkId rb;
        L->top = base + c + 1;  /* mark the end of concat operands */
        Protect(vsV_concat(L, c - b + 1));
        ra = RA(i);  /* 'vsV_concat' may invoke TMs and move the stack */
        rb = base + b;
        setobjs2s(L, ra, rb);
        checkGC(L, (ra >= rb ? ra + 1 : rb));
        L->top = ci->top;  /* restore top */
        vmbreak;
      }
      vmcase(OP_JMP) {
        dojump(ci, i, 0);
        vmbreak;
      }
      vmcase(OP_EQ) {
        TValue *rb = RKB(i);
        TValue *rc = RKC(i);
        Protect(
          if (vsV_equalobj(rb, rc) != GETARG_A(i))
            ci->savedpc++;
          else
            donextjump(ci);
        )
        vmbreak;
      }
      vmcase(OP_LT) {
        Protect(
          if (vsV_lessthan(L, RKB(i), RKC(i)) != GETARG_A(i))
            ci->savedpc++;
          else
            donextjump(ci);
        )
        vmbreak;
      }
      vmcase(OP_LE) {
        Protect(
          if (vsV_lessequal(L, RKB(i), RKC(i)) != GETARG_A(i))
            ci->savedpc++;
          else
            donextjump(ci);
        )
        vmbreak;
      }
      vmcase(OP_TEST) {
        if (GETARG_C(i) ? l_isfalse(ra) : !l_isfalse(ra))
            ci->savedpc++;
          else
          donextjump(ci);
        vmbreak;
      }
      vmcase(OP_TESTSET) {
        TValue *rb = RB(i);
        if (GETARG_C(i) ? l_isfalse(rb) : !l_isfalse(rb))
          ci->savedpc++;
        else {
          setobjs2s(L, ra, rb);
          donextjump(ci);
        }
        vmbreak;
      }
      vmcase(OP_CALL) {
        int b = GETARG_B(i);
        int nresults = GETARG_C(i) - 1;
        if (b != 0) L->top = ra+b;  /* else previous instruction set top */
        if (vsD_precall(L, ra, nresults)) {  /* C function? */
          if (nresults >= 0)
            L->top = ci->top;  /* adjust results */
          Protect((void)0);  /* update 'base' */
        }
        else {  /* VS function */
          // 在上面vsD_precall中修改了L->ci的指向
          ci = L->ci;
          // 每次新调用一个vs函数就重新开始for循环
          goto newframe;  /* restart vsV_execute over new VS function */
        }
        vmbreak;
      }
      vmcase(OP_TAILCALL) {
        int b = GETARG_B(i);
        if (b != 0) L->top = ra+b;  /* else previous instruction set top */
        vs_assert(GETARG_C(i) - 1 == VS_MULTRET);
        if (vsD_precall(L, ra, VS_MULTRET)) {  /* C function? */
          Protect((void)0);  /* update 'base' */
        }
        else {
          /* tail call: put called frame (n) in place of caller one (o) */
          CallInfo *nci = L->ci;  /* called frame */
          CallInfo *oci = nci->previous;  /* caller frame */
          StkId nfunc = nci->func;  /* called function */
          StkId ofunc = oci->func;  /* caller function */
          /* last stack slot filled by 'precall' */
          StkId lim = nci->base + getproto(nfunc)->numparams;
          int aux;
          /* close all upvalues from previous call */
          if (cl->p->sizep > 0) vsF_close(L, oci->base);
          /* move new frame into old one */
          for (aux = 0; nfunc + aux < lim; aux++)
            setobjs2s(L, ofunc + aux, nfunc + aux);
          oci->base = ofunc + (nci->base - nfunc);  /* correct base */
          oci->top = L->top = ofunc + (L->top - nfunc);  /* correct top */
          oci->savedpc = nci->savedpc;
          oci->callstatus |= CIST_TAIL;  /* function was tail called */
          ci = L->ci = oci;  /* remove new frame */
          vs_assert(L->top == oci->base + getproto(ofunc)->maxstacksize);
          goto newframe;  /* restart vsV_execute over new VS function */
        }
        vmbreak;
      }
      vmcase(OP_RETURN) {
        int b = GETARG_B(i);
        if (cl->p->sizep > 0) vsF_close(L, base);
        // 返回值的个数是b-1,如果b是0 返回值就是从A到栈顶
        b = vsD_poscall(L, ci, ra, (b != 0 ? b - 1 : cast_int(L->top - ra)));
        // 只有第一次执行的ci标记了CIST_FRESH
        // 当前已经到了最外部函数,执行结束
        if (ci->callstatus & CIST_FRESH)  /* local 'ci' still from callee */
          return;  /* external invocation: return */
        else {  /* invocation via reentry: continue execution */
          // 设置ci为外部函数
          ci = L->ci;
          if (b) L->top = ci->top;
          vs_assert(isVS(ci));
          // 现在ci是外部函数
          // 因为刚从函数调用退出来,ci即将执行的指令的前一条必定是OP_CALL
          vs_assert(GET_OPCODE(*((ci)->savedpc - 1)) == OP_CALL);
          goto newframe;  /* restart vsV_execute over new VS function */
        }
      }
      vmcase(OP_FORLOOP) {
        if (ttisinteger(ra)) {  /* integer loop? */
          vs_Integer step = ivalue(ra + 2);
          vs_Integer idx = intop(+, ivalue(ra), step); /* increment index */
          vs_Integer limit = ivalue(ra + 1);
          if ((0 < step) ? (idx <= limit) : (limit <= idx)) {
            ci->savedpc += GETARG_sBx(i);  /* jump back */
            chgivalue(ra, idx);  /* update internal index... */
            setivalue(ra + 3, idx);  /* ...and external index */
          }
        }
        else {  /* floating loop */
          vs_Number step = fltvalue(ra + 2);
          vs_Number idx = vsi_numadd(L, fltvalue(ra), step); /* inc. index */
          vs_Number limit = fltvalue(ra + 1);
          if (vsi_numlt(0, step) ? vsi_numle(idx, limit)
                                  : vsi_numle(limit, idx)) {
            ci->savedpc += GETARG_sBx(i);  /* jump back */
            chgfltvalue(ra, idx);  /* update internal index... */
            setfltvalue(ra + 3, idx);  /* ...and external index */
          }
        }
        vmbreak;
      }
      vmcase(OP_FORPREP) {
        TValue *init = ra;
        TValue *plimit = ra + 1;
        TValue *pstep = ra + 2;
        vs_Integer ilimit;
        int stopnow;
        if (ttisinteger(init) && ttisinteger(pstep) &&
            forlimit(plimit, &ilimit, ivalue(pstep), &stopnow)) {
          /* all values are integer */
          vs_Integer initv = (stopnow ? 0 : ivalue(init));
          setivalue(plimit, ilimit);
          setivalue(init, intop(-, initv, ivalue(pstep)));
        }
        else {  /* try making all values floats */
          vs_Number ninit; vs_Number nlimit; vs_Number nstep;
          if (!tonumber(plimit, &nlimit))
            vsG_runerror(L, "'for' limit must be a number");
          setfltvalue(plimit, nlimit);
          if (!tonumber(pstep, &nstep))
            vsG_runerror(L, "'for' step must be a number");
          setfltvalue(pstep, nstep);
          if (!tonumber(init, &ninit))
            vsG_runerror(L, "'for' initial value must be a number");
          setfltvalue(init, vsi_numsub(L, ninit, nstep));
        }
        ci->savedpc += GETARG_sBx(i);
        vmbreak;
      }
      vmcase(OP_TFORCALL) {
        StkId cb = ra + 3;  /* call base */
        setobjs2s(L, cb+2, ra+2);
        setobjs2s(L, cb+1, ra+1);
        setobjs2s(L, cb, ra);
        L->top = cb + 3;  /* func. + 2 args (state and index) */
        Protect(vsD_call(L, cb, GETARG_C(i)));
        L->top = ci->top;
        i = *(ci->savedpc++);  /* go to next instruction */
        ra = RA(i);
        vs_assert(GET_OPCODE(i) == OP_TFORLOOP);
        goto l_tforloop;
      }
      vmcase(OP_TFORLOOP) {
        l_tforloop:
        if (!ttisnil(ra + 1)) {  /* continue loop? */
          setobjs2s(L, ra, ra + 1);  /* save control variable */
           ci->savedpc += GETARG_sBx(i);  /* jump back */
        }
        vmbreak;
      }
      vmcase(OP_SETLIST) {
        int n = GETARG_B(i);
        int c = GETARG_C(i);
        unsigned int last;
        Table *h;
        if (n == 0) n = cast_int(L->top - ra) - 1;
        if (c == 0) {
          vs_assert(GET_OPCODE(*ci->savedpc) == OP_EXTRAARG);
          c = GETARG_Ax(*ci->savedpc++);
        }
        h = hvalue(ra);
        last = ((c-1)*LFIELDS_PER_FLUSH) + n;
        if (last > h->sizearray)  /* needs more space? */
          vsH_resizearray(L, h, last);  /* preallocate it at once */
        for (; n > 0; n--) {
          TValue *val = ra+n;
          vsH_setint(L, h, last--, val);
          vsC_barrierback(L, h, val);
        }
        L->top = ci->top;  /* correct top (in case of previous open call) */
        vmbreak;
      }
      vmcase(OP_CLOSURE) {
        // 找到要创建的函数的原型
        Proto *p = cl->p->p[GETARG_Bx(i)];
        // 先查询缓存
        LClosure *ncl = getcached(p, cl->upvals, base);  /* cached closure */
        if (ncl == NULL)  /* no match? */
          // 没有缓存结果 生成函数闭包
          pushclosure(L, p, cl->upvals, base, ra);  /* create a new one */
        else
          // 有缓存结果 直接写入寄存器
          setclLvalue(L, ra, ncl);  /* push cashed closure */
        checkGC(L, ra + 1);
        vmbreak;
      }
      vmcase(OP_VARARG) {
        // B-1是读取的结果个数
        int b = GETARG_B(i) - 1;  /* required results */
        int j;
        // 计算出...代表的结果的个数,也就是调用函数的时候给...传递的参数个数
        int n = cast_int(base - ci->func) - cl->p->numparams - 1;
        if (n < 0)  /* less arguments than parameters? */
          n = 0;  /* no vararg arguments */
        // b<0也就是B传入的0,代表获取可变参数的所有结果
        if (b < 0) {  /* B == 0? */
          b = n;  /* get all var. arguments */
          Protect(vsD_checkstack(L, n));
          ra = RA(i);  /* previous call may change the stack */
          // 从A寄存器开始写入...的所有结果,并设置栈顶
          L->top = ra + n;
        }
        for (j = 0; j < b && j < n; j++)
          setobjs2s(L, ra + j, base - n + j);
        for (; j < b; j++)  /* complete required results with nil */
          setnilvalue(ra + j);
        vmbreak;
      }
      vmcase(OP_EXTRAARG) {
        vs_assert(0);
        vmbreak;
      }
    }
  }
}

/* }================================================================== */

