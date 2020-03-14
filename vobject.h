/*
** $Id: lobject.h,v 2.117 2016/08/01 19:51:24 roberto Exp $
** Type definitions for Lua objects
** See Copyright Notice in vs.h
*/


#ifndef vobject_h
#define vobject_h

#include "vs.h"
#include "vlimits.h"




/*
** Extra tags for non-values
*/
// VS_TPROTO是额外的标记,本身不带任何值,不计算在值类型中
#define VS_TPROTO	VS_NUMTAGS		/* function prototypes 函数原型*/
// 如果一个key对应的value被设置为nil了,这个key就会被标记为VS_TDEADKEY
#define VS_TDEADKEY	(VS_NUMTAGS+1)		/* removed keys in tables 元表中删除的key */

/*
** number of all possible tags (including VS_TNONE but excluding DEADKEY)
*/
// 算上VS_TNONE还有VS_TPROTO总共11个
#define VS_TOTALTAGS	(VS_TPROTO + 2)


/*
** tags for Tagged Values have the following use of bits:
** bits 0-3: actual tag (a VS_T* value)
** bits 4-5: variant bits
** bit 6: whether value is collectable
*/
// vs数据类型使用6位进行标记,0-3位表示真实类型 也就是上面的9种类型
// 4-5位表示上面9种类型的子类型
// 6位表示该类型是否可以进行回收


/*
** VS_TFUNCTION variants:
** 0 - Lua function
** 1 - light C function
** 2 - regular C function (closure)
*/

/* Variant tags for functions */
// 函数类型分为 vs闭包 轻量级c函数 c函数闭包
#define VS_TLCL	(VS_TFUNCTION | (0 << 4))  /* Lua closure */
#define VS_TLCF	(VS_TFUNCTION | (1 << 4))  /* light C function */
#define VS_TCCL	(VS_TFUNCTION | (2 << 4))  /* C closure */


/* Variant tags for strings */
// 字符串类型分为 短字符串 长字符串
#define VS_TSHRSTR	(VS_TSTRING | (0 << 4))  /* short strings */
#define VS_TLNGSTR	(VS_TSTRING | (1 << 4))  /* long strings */


/* Variant tags for numbers */
// 数字类型分为 浮点数 整数
#define VS_TNUMFLT	(VS_TNUMBER | (0 << 4))  /* float numbers */
#define VS_TNUMINT	(VS_TNUMBER | (1 << 4))  /* integer numbers */


/* Bit mark for collectable types */
#define BIT_ISCOLLECTABLE	(1 << 6)

/* mark a tag as collectable */
// 用于标记一个类型为可回收类型
#define ctb(t)			((t) | BIT_ISCOLLECTABLE)


/*
** Common type for all collectable objects
*/
typedef struct GCObject GCObject;


/*
** Common Header for all collectable objects (in macro form, to be
** included in other objects)
*/
// 所有的 GCObject 都有一个相同的数据头，叫作 CommonHeader以宏形式定义出来的
// 使用宏是源于使用上的某种便利 C语言不支持结构的继承
// 所有的 GCObject 都用一个单向链表串了起来。每个对象都以 tt 来识别其类型。marked 域用于标记清除的工作
#define CommonHeader	GCObject *next; lu_byte tt; lu_byte marked


/*
** Common type has only the common header
*/
struct GCObject {
  CommonHeader;
};


// 这是vs所有数据类型的联合, 使用它来表示vs的数据类型
typedef union Value {
  GCObject *gc;    /* collectable objects */
  void *p;         /* light userdata */
  int b;           /* booleans */
  vs_CFunction f; /* light C functions */
  vs_Integer i;   /* integer numbers */
  vs_Number n;    /* float numbers */
} Value;


#define TValuefields	Value value_; int tt_

// 使用value_表示真实值, tt_标记类型
// tt_是type tag的简写,复合类型，用来表示类型
typedef struct vs_TValue {
  TValuefields;
} TValue;



/* macro defining a nil value */
// 就是0
#define NILCONSTANT	{NULL}, VS_TNIL


// 取出TValue类型的真实value_值
#define val_(o)		((o)->value_)


/* raw type tag of a TValue */
// 取出原始type tag
#define rttype(o)	((o)->tt_)

/* tag with no variants (bits 0-3) */
// 去掉变体部分 只有最后4位 即主类型
#define novariant(x)	((x) & 0x0F)

/* type tag of a TValue (bits 0-3 for tags + variant bits 4-5) */
// 主类型加变体类型
#define ttype(o)	(rttype(o) & 0x3F)

/* type tag of a TValue with no variants (bits 0-3) */
// 取出主类型
#define ttnov(o)	(novariant(rttype(o)))


/* Macros to test type */
#define checktag(o,t)		(rttype(o) == (t))  // 检查主类型加变体类型
#define checktype(o,t)		(ttnov(o) == (t)) // 只检查主类型
#define ttisnumber(o)		checktype((o), VS_TNUMBER)
#define ttisfloat(o)		checktag((o), VS_TNUMFLT)
#define ttisinteger(o)		checktag((o), VS_TNUMINT)
#define ttisnil(o)		checktag((o), VS_TNIL)
#define ttisboolean(o)		checktag((o), VS_TBOOLEAN)
#define ttislightuserdata(o)	checktag((o), VS_TLIGHTUSERDATA)
#define ttisstring(o)		checktype((o), VS_TSTRING)
#define ttisshrstring(o)	checktag((o), ctb(VS_TSHRSTR))
#define ttislngstring(o)	checktag((o), ctb(VS_TLNGSTR))
#define ttistable(o)		checktag((o), ctb(VS_TTABLE))
#define ttisfunction(o)		checktype(o, VS_TFUNCTION)
// 闭包有vs闭包和c闭包
// 要检查vs类型是否是闭包 首先必须是VS_TFUNCTION
// 如果是VS_TFUNCTION还有三种可能 vs闭包 轻量级c函数 c闭包
// 只有轻量级c闭包倒数第5位是1
// 00 0110 vs闭包
// 01 0110 轻量级c函数
// 10 0110 c闭包
#define ttisclosure(o)		((rttype(o) & 0x1F) == VS_TFUNCTION)
#define ttisCclosure(o)		checktag((o), ctb(VS_TCCL))
#define ttisLclosure(o)		checktag((o), ctb(VS_TLCL))
#define ttislcf(o)		checktag((o), VS_TLCF)
#define ttisfulluserdata(o)	checktag((o), ctb(VS_TUSERDATA))
#define ttisthread(o)		checktag((o), ctb(VS_TTHREAD))
#define ttisdeadkey(o)		checktag((o), VS_TDEADKEY)


/* Macros to access values */
// 不是调试模式时check_exp没有意义, 以下地方都可以忽略只看第二个参数
#define ivalue(o)	check_exp(ttisinteger(o), val_(o).i)
#define fltvalue(o)	check_exp(ttisfloat(o), val_(o).n)
// 取出vs_Number 如果取出的整数 进行一次强制类型转换
// 正常情况就是long long 转换成double
#define nvalue(o)	check_exp(ttisnumber(o), \
	(ttisinteger(o) ? cast_num(ivalue(o)) : fltvalue(o)))
// 需要进行gc的类型存在 o->value_.gc里
#define gcvalue(o)	check_exp(iscollectable(o), val_(o).gc)
#define pvalue(o)	check_exp(ttislightuserdata(o), val_(o).p)
// 需要取出具体的gc类型需要进行类型转换
#define tsvalue(o)	check_exp(ttisstring(o), gco2ts(val_(o).gc))
#define uvalue(o)	check_exp(ttisfulluserdata(o), gco2u(val_(o).gc))
#define clvalue(o)	check_exp(ttisclosure(o), gco2cl(val_(o).gc))
#define clLvalue(o)	check_exp(ttisLclosure(o), gco2lcl(val_(o).gc))
#define clCvalue(o)	check_exp(ttisCclosure(o), gco2ccl(val_(o).gc))
// 轻量级c函数
#define fvalue(o)	check_exp(ttislcf(o), val_(o).f)
// 表类型
#define hvalue(o)	check_exp(ttistable(o), gco2t(val_(o).gc))
#define bvalue(o)	check_exp(ttisboolean(o), val_(o).b)
#define thvalue(o)	check_exp(ttisthread(o), gco2th(val_(o).gc))
/* a dead value may get the 'gc' field, but cannot access its contents */
#define deadvalue(o)	check_exp(ttisdeadkey(o), cast(void *, val_(o).gc))

#define l_isfalse(o)	(ttisnil(o) || (ttisboolean(o) && bvalue(o) == 0))


#define iscollectable(o)	(rttype(o) & BIT_ISCOLLECTABLE)


/* Macros for internal tests */
// 检测type tag是否正确
// 将自己的type tag 与 从gc类型中取到的type tag进行比对
#define righttt(obj)		(ttype(obj) == gcvalue(obj)->tt)

// 为真的条件
// 1.不是垃圾回收类型
// 2.isdead为否
#define checkliveness(L,obj) \
	vs_longassert(!iscollectable(obj) || \
		(righttt(obj) && (L == NULL || !isdead(G(L),gcvalue(obj)))))


/* Macros to set values */
// 一系列宏 用于赋值
// 主要操作是修改真实值以及修改type tag
#define settt_(o,t)	((o)->tt_=(t))

#define setfltvalue(obj,x) \
  { TValue *io=(obj); val_(io).n=(x); settt_(io, VS_TNUMFLT); }

#define chgfltvalue(obj,x) \
  { TValue *io=(obj); vs_assert(ttisfloat(io)); val_(io).n=(x); }

#define setivalue(obj,x) \
  { TValue *io=(obj); val_(io).i=(x); settt_(io, VS_TNUMINT); }

#define chgivalue(obj,x) \
  { TValue *io=(obj); vs_assert(ttisinteger(io)); val_(io).i=(x); }

#define setnilvalue(obj) settt_(obj, VS_TNIL)

#define setfvalue(obj,x) \
  { TValue *io=(obj); val_(io).f=(x); settt_(io, VS_TLCF); }

#define setpvalue(obj,x) \
  { TValue *io=(obj); val_(io).p=(x); settt_(io, VS_TLIGHTUSERDATA); }

#define setbvalue(obj,x) \
  { TValue *io=(obj); val_(io).b=(x); settt_(io, VS_TBOOLEAN); }

#define setgcovalue(L,obj,x) \
  { TValue *io = (obj); GCObject *i_g=(x); \
    val_(io).gc = i_g; settt_(io, ctb(i_g->tt)); }

// obj2gco只是进行了一次强制类型转换 将各种可回收类型指针 转换成 GCObject*类型
#define setsvalue(L,obj,x) \
  { TValue *io = (obj); TString *x_ = (x); \
    val_(io).gc = obj2gco(x_); settt_(io, ctb(x_->tt)); \
    checkliveness(L,io); }

#define setuvalue(L,obj,x) \
  { TValue *io = (obj); Udata *x_ = (x); \
    val_(io).gc = obj2gco(x_); settt_(io, ctb(VS_TUSERDATA)); \
    checkliveness(L,io); }

#define setthvalue(L,obj,x) \
  { TValue *io = (obj); vs_State *x_ = (x); \
    val_(io).gc = obj2gco(x_); settt_(io, ctb(VS_TTHREAD)); \
    checkliveness(L,io); }

#define setclLvalue(L,obj,x) \
  { TValue *io = (obj); LClosure *x_ = (x); \
    val_(io).gc = obj2gco(x_); settt_(io, ctb(VS_TLCL)); \
    checkliveness(L,io); }

#define setclCvalue(L,obj,x) \
  { TValue *io = (obj); CClosure *x_ = (x); \
    val_(io).gc = obj2gco(x_); settt_(io, ctb(VS_TCCL)); \
    checkliveness(L,io); }

#define sethvalue(L,obj,x) \
  { TValue *io = (obj); Table *x_ = (x); \
    val_(io).gc = obj2gco(x_); settt_(io, ctb(VS_TTABLE)); \
    checkliveness(L,io); }

#define setdeadvalue(obj)	settt_(obj, VS_TDEADKEY)


// 直接进行赋值
#define setobj(L,obj1,obj2) \
	{ TValue *io1=(obj1); *io1 = *(obj2); \
	  (void)L; checkliveness(L,io1); }


/*
** different types of assignments, according to destination
*/

// 以下宏只是对上面各种赋值宏的重命名 更好的语义化
// 功能也是进行值复制
/* from stack to (same) stack */
#define setobjs2s	setobj
/* to stack (not from same stack) */
#define setobj2s	setobj
#define setsvalue2s	setsvalue
#define sethvalue2s	sethvalue
#define setptvalue2s	setptvalue
/* from table to same table */
#define setobjt2t	setobj
/* to new object */
#define setobj2n	setobj
#define setsvalue2n	setsvalue

/* to table (define it as an expression to be used in macros) */
#define setobj2t(L,o1,o2)  ((void)L, *(o1)=*(o2), checkliveness(L,(o1)))




/*
** {======================================================
** types and prototypes
** =======================================================
*/


typedef TValue *StkId;  /* index to stack elements */




/*
** Header for string value; string bytes follow the end of this structure
** (aligned according to 'UTString'; see next).
*/
typedef struct TString {
  CommonHeader;
  // 对于短字符串 extra记录了关键字的位置从1开始 and为1 break为2 ... 顺序在vsX_tokens中
  // 是不支持自动回收的,在GC过程中会略过对这个字符串的处理 vsX_tokens
  // 对于长字符串 用于标记已经计算了哈希值
  lu_byte extra;  /* reserved words for short strings; "has hash" for longs */
  // 短字符串的长度
  lu_byte shrlen;  /* length for short strings */
  // 字符串的hash值,字符串的比较可以通过hash值
  unsigned int hash;
  // 对于长字符串这里记录字符串长度
  // 对于短字符串这里记录下一个的地址
  union {
    size_t lnglen;  /* length for long strings */
    struct TString *hnext;  /* linked list for hash table */
  } u;
} TString;


/*
** Ensures that address after this type is always fully aligned.
*/
// 只是用来计算sizeof(UTString)代表TString的大小 没有其他作用
typedef union UTString {
  L_Umaxalign dummy;  /* ensures maximum alignment for strings */
  TString tsv;
} UTString;


/*
** Get the actual string (array of bytes) from a 'TString'.
** (Access to 'extra' ensures that value is really a 'TString'.)
*/
// 真实的字符串就存储在TString后面, 加上UTString的大小即可
#define getstr(ts)  \
  check_exp(sizeof((ts)->extra), cast(char *, (ts)) + sizeof(UTString))


/* get the actual string (array of bytes) from a Lua value */
// 从TValue类型取出真实字符串
#define svalue(o)       getstr(tsvalue(o))

/* get string length from 'TString *s' */
// 从TString获取字符串长度
#define tsslen(s)	((s)->tt == VS_TSHRSTR ? (s)->shrlen : (s)->u.lnglen)

/* get string length from 'TValue *o' */
// 从TValue获取字符串长度
#define vslen(o)	tsslen(tsvalue(o))


/*
** Header for userdata; memory area follows the end of this structure
** (aligned according to 'UUdata'; see next).
*/
typedef struct Udata {
  // commonHeader中的tt 指定了类型位Udata
  CommonHeader;
  // 关联的值类型,和下面的user_组合
  lu_byte ttuv_;  /* user value's tag */
  // 申请的内存块的大小
  size_t len;  /* number of bytes */
  // 关联值,实际存放数据的地方
  union Value user_;  /* user value */
} Udata;


/*
** Ensures that address after this type is always fully aligned.
*/
typedef union UUdata {
  L_Umaxalign dummy;  /* ensures maximum alignment for 'local' udata */
  Udata uv;
} UUdata;


/*
**  Get the address of memory block inside 'Udata'.
** (Access to 'ttuv_' ensures that value is really a 'Udata'.)
*/
#define getudatamem(u)  \
  check_exp(sizeof((u)->ttuv_), (cast(char*, (u)) + sizeof(UUdata)))

// 不论是get还是set都是两步 1.设置值 2.设置type tag
// 把o的值设置到u上 o是TValue,u是Udata
#define setuservalue(L,u,o) \
	{ const TValue *io=(o); Udata *iu = (u); \
	  iu->user_ = io->value_; iu->ttuv_ = rttype(io); \
	  checkliveness(L,io); }

// 把u的值写到o上 o是TValue,u是Udata
#define getuservalue(L,u,o) \
	{ TValue *io=(o); const Udata *iu = (u); \
	  io->value_ = iu->user_; settt_(io, iu->ttuv_); \
	  checkliveness(L,io); }


/*
** Description of an upvalue for function prototypes
*/
// upvalue的描述信息
typedef struct Upvaldesc {
  // upvalue的名称
  TString *name;  /* upvalue name (for debug information) */
  // 该upvalue是不是父函数的局部变量
  // 如果是父函数的局部变量说明该upvalue就在父函数的栈上
  lu_byte instack;  /* whether it is in stack (register) */
  // 索引位置信息
  // instack为1 idx代表父函数栈上位置
  // instack为0 idx代表父函数upval数组的下标(LClosure.upvals)
  lu_byte idx;  /* index of upvalue (in stack or in outer function's list) */
} Upvaldesc;


/*
** Description of a local variable for function prototypes
** (used for debug information)
*/
// 局部变量的描述信息
typedef struct LocVar {
  TString *varname; // 变量名称
  // start和end指定了变量的作用域
  int startpc;  /* first point where variable is active 变量作用域开始位置 */
  int endpc;    /* first point where variable is dead   变量作用域结束位置 */
} LocVar;



// 函数原型
typedef struct Proto {
  CommonHeader;
  lu_byte numparams;  /* number of fixed parameters */
  lu_byte is_vararg;
  lu_byte maxstacksize;  /* number of registers needed by this function */
  int sizeupvalues;  /* size of 'upvalues' */
  int sizek;  /* size of 'k' */
  int sizecode;
  int sizelineinfo;
  // 子函数数组的长度
  int sizep;  /* size of 'p' */
  int sizelocvars;
  int linedefined;  /* debug information  */
  int lastlinedefined;  /* debug information  */
  TValue *k;  /* constants used by the function */
  Instruction *code;  /* opcodes */
  // 子函数数组
  struct Proto **p;  /* functions defined inside the function */
  int *lineinfo;  /* map from opcodes to source lines (debug information) */
  LocVar *locvars;  /* information about local variables (debug information) */
  Upvaldesc *upvalues;  /* upvalue information */
  struct LClosure *cache;  /* last-created closure with this prototype */
  TString  *source;  /* used for debug information */
  GCObject *gclist;
} Proto;



/*
** Lua Upvalues
*/
typedef struct UpVal UpVal;


/*
** Closures
*/
// 闭包都属于VS_TFUNCTION类型

// nupvalues指定upvalue的个数
#define ClosureHeader \
	CommonHeader; lu_byte nupvalues; GCObject *gclist

// vsF_newCclosure
// c函数闭包 包括一个c函数指针和相关的upvalue
typedef struct CClosure {
  ClosureHeader;
  vs_CFunction f;
  TValue upvalue[1];  /* list of upvalues */
} CClosure;


// vsF_newLclosure
// vs闭包 包括一个vs函数原型和相关的upvalue
typedef struct LClosure {
  ClosureHeader;
  struct Proto *p;
  UpVal *upvals[1];  /* list of upvalues */
} LClosure;


// 统一两种闭包类型
typedef union Closure {
  CClosure c;
  LClosure l;
} Closure;


// 检查是否是vs闭包
#define isLfunction(o)	ttisLclosure(o)

// 获取函数原型 就是获取vs闭包中的proto
#define getproto(o)	(clLvalue(o)->p)


typedef union TKey {
  struct {
    TValuefields;
    int next;  /* for chaining (offset for next node) 这个字段记录了与下一个节点的偏移量 */
  } nk;
  TValue tvk;
} TKey;


/* copy a value into a key without messing up field 'next' */
// 向TKey类型写入一个TValue
// 直接写入tvk会导致next被覆盖
#define setnodekey(L,key,obj) \
	{ TKey *k_=(key); const TValue *io_=(obj); \
	  k_->nk.value_ = io_->value_; k_->nk.tt_ = io_->tt_; \
	  (void)L; checkliveness(L,io_); }


// Node就是table的一项 分为键和值
typedef struct Node {
  TValue i_val;
  TKey i_key;
} Node;


typedef struct Table {
  CommonHeader;
  // lsizenode 字段是该表Hash桶大小的log2值,Hash桶数组大小一定是2的次方,当扩展Hash桶的时候每次需要乘以2。
  lu_byte lsizenode;  /* log2 of size of 'node' array */
  // sizearray 字段表示该表数组部分的size
  unsigned int sizearray;  /* size of 'array' array */
  // array 指向该表的数组部分的起始位置。
  TValue *array;  /* array part */
  // node 指向该表的Hash部分的起始位置。
  // node部分使用了开放定址法实现哈希表
  Node *node;
  // lastfree 指向Lua表的Hash 部分的末尾位置。
  Node *lastfree;  /* any free position is before this position */
  // gclist GC相关的链表。 
  GCObject *gclist;
} Table;



// 求s%size 由于size一定是2的指数 所以使用了下面的位运算加速
// 当b是2的指数时 mod(a,b) 与 a&(b-1)等价 一般的编译器也会对这种情况进行优化
#define lmod(s,size) \
	(check_exp((size&(size-1))==0, (cast(int, (s) & ((size)-1)))))

// 2的x次方
#define twoto(x)	(1<<(x))
// 哈希表部分桶的个数
#define sizenode(t)	(twoto((t)->lsizenode))


// 全局唯一的nil变量
#define vsO_nilobject		(&vsO_nilobject_)


VSI_DDEC const TValue vsO_nilobject_;

/* size of buffer for 'vsO_utf8esc' function */
// 设置vsO_utf8esc函数缓存区的大小
#define UTF8BUFFSZ	8

VSI_FUNC int vsO_int2fb (unsigned int x);
VSI_FUNC int vsO_fb2int (int x);
VSI_FUNC int vsO_utf8esc (char *buff, unsigned long x);
VSI_FUNC int vsO_ceillog2 (unsigned int x);
VSI_FUNC void vsO_arith (vs_State *L, int op, const TValue *p1,
                           const TValue *p2, TValue *res);
VSI_FUNC size_t vsO_str2num (const char *s, TValue *o);
VSI_FUNC int vsO_hexavalue (int c);
VSI_FUNC void vsO_tostring (vs_State *L, StkId obj);
VSI_FUNC const char *vsO_pushvfstring (vs_State *L, const char *fmt,
                                                       va_list argp);
VSI_FUNC const char *vsO_pushfstring (vs_State *L, const char *fmt, ...);
VSI_FUNC void vsO_chunkid (char *out, const char *source, size_t len);


#endif

