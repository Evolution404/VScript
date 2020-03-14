#ifndef vs_h
#define vs_h

#include <stdarg.h>
#include <stddef.h>

#include "vsconf.h"

// vs的一些版本信息
#define VS_VERSION_MAJOR	"1"
#define VS_VERSION_MINOR	"0"
#define VS_VERSION_NUM		100
#define VS_VERSION_RELEASE	"1"

#define VS_VERSION	"VS " VS_VERSION_MAJOR "." VS_VERSION_MINOR
#define VS_RELEASE	VS_VERSION "." VS_VERSION_RELEASE
#define VS_COPYRIGHT	VS_RELEASE "  Copyright (C) 2019-2020 zhangyuxi.xyz, HIT"
#define VS_AUTHORS	"ZhangYuxi"

/* mark for precompiled code ('<esc>VS') 放在预编译文件的头,用来标记预编译文件 */
#define VS_SIGNATURE	"\x1bVS"

// 多个返回值的标记
#define VS_MULTRET	(-1)


/*
** Pseudo-indices
** (-VSI_MAXSTACK is the minimum valid index; we keep some free empty
** space after that to help overflow detection)
*/
// 伪索引
#define VS_REGISTRYINDEX	(-VSI_MAXSTACK - 1000)
// upvalue的索引
#define vs_upvalueindex(i)	(VS_REGISTRYINDEX - (i))


/* thread status */
// 线程的各个状态
#define VS_OK		0
#define VS_YIELD	1
#define VS_ERRRUN	2
#define VS_ERRSYNTAX	3
#define VS_ERRMEM	4
#define VS_ERRGCMM	5
#define VS_ERRERR	6


typedef struct vs_State vs_State;


/*
** basic types 基本数据类型
*/
#define VS_TNONE		(-1)

#define VS_TNIL		0
#define VS_TBOOLEAN		1
#define VS_TLIGHTUSERDATA	2
#define VS_TNUMBER		3
#define VS_TSTRING		4
#define VS_TTABLE		5
#define VS_TFUNCTION		6
#define VS_TUSERDATA		7
#define VS_TTHREAD		8

#define VS_NUMTAGS		9



/* minimum VS stack available to a C function */
// 调用c函数时vs栈剩余的最小空间
#define VS_MINSTACK	20


/* predefined values in the registry */
// G(L)->l_registry是一个具有长度为2的数组的表
// t[1]放主线程 t[2]放全局注册表
#define VS_RIDX_MAINTHREAD	1
#define VS_RIDX_GLOBALS 2
#define VS_RIDX_LAST		VS_RIDX_GLOBALS


/* type of numbers in VS */
typedef VS_NUMBER vs_Number;      // double


/* type for integer functions */
typedef VS_INTEGER vs_Integer;    // long long

/* unsigned integer type */
typedef VS_UNSIGNED vs_Unsigned;  // unsigned long long

// c函数
typedef int (*vs_CFunction) (vs_State *L);

// 用于读取和写入vs块的函数,只有一次用到其实就是getF函数
typedef const char * (*vs_Reader) (vs_State *L, void *ud, size_t *sz);

typedef int (*vs_Writer) (vs_State *L, const void *p, size_t sz, void *ud);


/*
** Type for memory-allocation functions
*/
// 内存分配函数
typedef void * (*vs_Alloc) (void *ud, void *ptr, size_t osize, size_t nsize);


// 线程操作,定义在vstate.c中
// 创建线程
VS_API vs_State *(vs_newstate) (vs_Alloc f, void *ud);
// 销毁线程
VS_API void       (vs_close) (vs_State *L);

VS_API vs_CFunction (vs_atpanic) (vs_State *L, vs_CFunction panicf);

// vapi的函数定义
VS_API const vs_Number *(vs_version) (vs_State *L);

// 基本栈操作
// 将其他格式idx转换成相对于L->ci->func的位置
VS_API int   (vs_absindex) (vs_State *L, int idx);
// 返回top-1位置的idx
VS_API int   (vs_gettop) (vs_State *L);
// 修改top指向
// 正数: top = func+1+idx  传入0 top指向func+1
// 负数: top = top+1+idx   传入-1 top指向top 传入-2 top指向top-1
VS_API void  (vs_settop) (vs_State *L, int idx);
// 将idx位置值压入栈顶
VS_API void  (vs_pushvalue) (vs_State *L, int idx);
// 将从idx开始到栈顶的值进行左移(n>0)或右移(n<0)
VS_API void  (vs_rotate) (vs_State *L, int idx, int n);
// 设置toidx的值位fromidx
VS_API void  (vs_copy) (vs_State *L, int fromidx, int toidx);
// 确保栈上至少还有n个空间 成功返回1 失败返回0
VS_API int   (vs_checkstack) (vs_State *L, int n);

/*
** access functions (stack -> C)
*/

VS_API int             (vs_isnumber) (vs_State *L, int idx);
VS_API int             (vs_isstring) (vs_State *L, int idx);
VS_API int             (vs_iscfunction) (vs_State *L, int idx);
VS_API int             (vs_isinteger) (vs_State *L, int idx);
VS_API int             (vs_isuserdata) (vs_State *L, int idx);
VS_API int             (vs_type) (vs_State *L, int idx);
VS_API const char     *(vs_typename) (vs_State *L, int tp);

VS_API vs_Number      (vs_tonumberx) (vs_State *L, int idx, int *isnum);
VS_API vs_Integer     (vs_tointegerx) (vs_State *L, int idx, int *isnum);
VS_API int             (vs_toboolean) (vs_State *L, int idx);
VS_API const char     *(vs_tolstring) (vs_State *L, int idx, size_t *len);
VS_API size_t          (vs_rawlen) (vs_State *L, int idx);
VS_API vs_CFunction   (vs_tocfunction) (vs_State *L, int idx);
VS_API void	       *(vs_touserdata) (vs_State *L, int idx);
VS_API vs_State      *(vs_tothread) (vs_State *L, int idx);
VS_API const void     *(vs_topointer) (vs_State *L, int idx);

/* ORDER TM, ORDER OP */
#define VS_OPADD	0    // 加法(+)
#define VS_OPSUB	1    // 减法(-)
#define VS_OPMUL	2    // 乘法(*)
#define VS_OPMOD	3    // 取模(%)
#define VS_OPPOW	4    // 乘方(^)
#define VS_OPDIV	5    // 向下取整的除法(//)
#define VS_OPIDIV	6  // 浮点除法(/)
#define VS_OPBAND	7  // 按位与(&)
#define VS_OPBOR	8    // 按位或(|)
#define VS_OPBXOR	9  // 按位异或(~)
#define VS_OPSHL	10   // 左移(<<)
#define VS_OPSHR	11   // 右移(>>)
#define VS_OPUNM	12   // 取负(一元-)
#define VS_OPBNOT	13 // 按位取反(~)

// 将栈顶的1个或2个操作数执行op运算 删除操作数栈顶保留运算结果
VS_API void  (vs_arith) (vs_State *L, int op);

#define VS_OPEQ	0
#define VS_OPLT	1
#define VS_OPLE	2

// 对index1和index2的值进行相等(VS_OPEQ),小于(VS_OPLT),小于等于(VS_OPLE)的比较
VS_API int   (vs_compare) (vs_State *L, int idx1, int idx2, int op);
#define vs_equal(L,idx1,idx2)		vs_compare(L,(idx1),(idx2),VS_OPEQ)


// 向栈顶压入不同类型的数据
VS_API void        (vs_pushnil) (vs_State *L);                                // nil
VS_API void        (vs_pushnumber) (vs_State *L, vs_Number n);               // 浮点数
VS_API void        (vs_pushinteger) (vs_State *L, vs_Integer n);             // 整数
VS_API const char *(vs_pushlstring) (vs_State *L, const char *s, size_t len); // 指定长度字符串
VS_API const char *(vs_pushstring) (vs_State *L, const char *s);              // 字符串
VS_API const char *(vs_pushvfstring) (vs_State *L, const char *fmt,           // 格式字符串
                                                      va_list argp);
VS_API const char *(vs_pushfstring) (vs_State *L, const char *fmt, ...);      // 格式字符串(可变参数形式)
VS_API void  (vs_pushcclosure) (vs_State *L, vs_CFunction fn, int n);        // c闭包
VS_API void  (vs_pushboolean) (vs_State *L, int b);                           // 布尔值
VS_API void  (vs_pushlightuserdata) (vs_State *L, void *p);                   // 轻量级用户数据
VS_API int   (vs_pushthread) (vs_State *L);                                   // 线程

// get函数返回值都是查询结果的类型

// 查询_G[name]的值并压入栈顶
VS_API int (vs_getglobal) (vs_State *L, const char *name);
// t为idx位置的表
// top-1=t[top-1] 修改栈顶的值为表中项
VS_API int (vs_gettable) (vs_State *L, int idx);
// 查询idx位置值的k键的值 压入栈顶
VS_API int (vs_getfield) (vs_State *L, int idx, const char *k);
// 设置top-1 = t[n]
VS_API int (vs_geti) (vs_State *L, int idx, vs_Integer n);
// top-1 = t[p] 查询结果压入栈顶,不查询元表
VS_API int (vs_getp) (vs_State *L, int idx, const void *p);

// 新建一个table对象放到栈顶
VS_API void  (vs_createtable) (vs_State *L, int narr, int nrec);
// 创建一个userdata压入栈顶 返回userdata的数据实际存放位置
VS_API void *(vs_newuserdata) (vs_State *L, size_t sz);
// 将idx位置用户数据压入栈顶
VS_API int  (vs_getuservalue) (vs_State *L, int idx);


/*
** set functions (stack -> VS)
*/
// 设置 _G[name] = 栈顶的值 弹出栈顶
VS_API void  (vs_setglobal) (vs_State *L, const char *name);
// t[top-2]=top-1 弹出 top-2和top-1
VS_API void  (vs_settable) (vs_State *L, int idx);
// t[k]=top-1 弹出top-1
VS_API void  (vs_setfield) (vs_State *L, int idx, const char *k);
// t[n]=top-1 弹出top-1
VS_API void  (vs_seti) (vs_State *L, int idx, vs_Integer n);
// t[p]=top-1 弹出栈顶 不查询元表
VS_API void  (vs_setp) (vs_State *L, int idx, const void *p);
// 设置idx位置k键的值为当前栈顶的值 弹出栈顶
VS_API void  (vs_setuservalue) (vs_State *L, int idx);


/*
** 'load' and 'call' functions (load and run VS code)
*/
VS_API void  (vs_call) (vs_State *L, int nargs, int nresults);

VS_API int   (vs_pcall) (vs_State *L, int nargs, int nresults, int errfunc);

VS_API int   (vs_load) (vs_State *L, vs_Reader reader, void *dt,
                          const char *chunkname, const char *mode);

VS_API int (vs_dump) (vs_State *L, vs_Writer writer, void *data, int strip);


/*
** miscellaneous functions
*/

VS_API int   (vs_error) (vs_State *L);

VS_API int   (vs_next) (vs_State *L, int idx);

VS_API void  (vs_concat) (vs_State *L, int n);
VS_API void  (vs_len)    (vs_State *L, int idx);

VS_API size_t   (vs_stringtonumber) (vs_State *L, const char *s);

VS_API vs_Alloc (vs_getallocf) (vs_State *L, void **ud);
VS_API void      (vs_setallocf) (vs_State *L, vs_Alloc f, void *ud);


// 一些方便使用的宏
#define vs_tonumber(L,i)	vs_tonumberx(L,(i),NULL)
#define vs_tointeger(L,i)	vs_tointegerx(L,(i),NULL)

// 将top向前移动n个
#define vs_pop(L,n)		vs_settop(L, -(n)-1)

#define vs_newtable(L)		vs_createtable(L, 0, 0)

#define vs_pushcfunction(L,f)	vs_pushcclosure(L, (f), 0)

#define vs_isfunction(L,n)	(vs_type(L, (n)) == VS_TFUNCTION)
#define vs_istable(L,n)	(vs_type(L, (n)) == VS_TTABLE)
#define vs_islightuserdata(L,n)	(vs_type(L, (n)) == VS_TLIGHTUSERDATA)
#define vs_isnil(L,n)		(vs_type(L, (n)) == VS_TNIL)
#define vs_isboolean(L,n)	(vs_type(L, (n)) == VS_TBOOLEAN)
#define vs_isthread(L,n)	(vs_type(L, (n)) == VS_TTHREAD)
#define vs_isnone(L,n)		(vs_type(L, (n)) == VS_TNONE)
#define vs_isnoneornil(L, n)	(vs_type(L, (n)) <= 0)

#define vs_pushliteral(L, s)	vs_pushstring(L, "" s)

#define vs_pushglobaltable(L)  \
	((void)vs_geti(L, VS_REGISTRYINDEX, VS_RIDX_GLOBALS))

#define vs_tostring(L,i)	vs_tolstring(L, (i), NULL)

#define vs_insert(L,idx)	vs_rotate(L, (idx), 1)

#define vs_remove(L,idx)	(vs_rotate(L, (idx), -1), vs_pop(L, 1))

#define vs_replace(L,idx)	(vs_copy(L, -1, (idx)), vs_pop(L, 1))


typedef struct vs_Debug vs_Debug;  /* activation record */

VS_API int (vs_getstack) (vs_State *L, int level, vs_Debug *ar);
VS_API int (vs_getinfo) (vs_State *L, const char *what, vs_Debug *ar);
//VS_API const char *(vs_getlocal) (vs_State *L, const vs_Debug *ar, int n);
//VS_API const char *(vs_setlocal) (vs_State *L, const vs_Debug *ar, int n);
VS_API const char *(vs_getupvalue) (vs_State *L, int funcindex, int n);
VS_API const char *(vs_setupvalue) (vs_State *L, int funcindex, int n);

struct vs_Debug {
  int event;
  const char *name;	/* (n) */
  const char *namewhat;	/* (n) 'global', 'local', 'field', 'method' */
  const char *what;	/* (S) 'VS', 'C', 'main', 'tail' */
  const char *source;	/* (S) */
  int currentline;	/* (l) */
  int linedefined;	/* (S) */
  int lastlinedefined;	/* (S) */
  unsigned char nups;	/* (u) number of upvalues */
  unsigned char nparams;/* (u) number of parameters */
  char isvararg;        /* (u) */
  char short_src[VS_IDSIZE]; /* (S) */
  /* private part */
  struct CallInfo *i_ci;  /* active function */
};

#endif
