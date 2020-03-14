#ifndef vlimits_h
#define vlimits_h


#include <limits.h>
#include <stddef.h>


#include "vs.h"

// lu_mem 无符号
// l_mem 有符号 这两个的范围要能用于表示内存
typedef size_t lu_mem;
typedef ptrdiff_t l_mem;


/* chars used as small naturals (so that 'char' is reserved for characters) */
typedef unsigned char lu_byte;


// size_t是无符号数, 按位取反得到它的最大值
/* maximum value for size_t */
#define MAX_SIZET	((size_t)(~(size_t)0))

/* maximum size visible for Lua (must be representable in a vs_Integer */
#define MAX_SIZE	(sizeof(size_t) < sizeof(vs_Integer) ? MAX_SIZET \
                          : (size_t)(VS_MAXINTEGER))


#define MAX_LUMEM	((lu_mem)(~(lu_mem)0))

#define MAX_LMEM	((l_mem)(MAX_LUMEM >> 1))


#define MAX_INT		INT_MAX  /* maximum value of an int */

// 只是为了求哈希用, 没有比较保留所有数据
#define point2uint(p)	((unsigned int)((size_t)(p) & UINT_MAX))



// 这个结构用于字节对齐
// 实际就是该平台的一个最大的数据类型
typedef union {
  vs_Number n;
  double u;
  void *s;
  vs_Integer i;
  long l;
} L_Umaxalign;



/* types of 'usual argument conversions' for vs_Number and vs_Integer */
typedef VSI_UACNUMBER l_uacNumber; // double
typedef VSI_UACINT l_uacInt;       // long long


// 测试宏
#include <assert.h>
#define vs_assert(c)		assert(c)
#define check_exp(c,e)		(e)
#define vs_longassert(c)	((void)0)

#define vsi_apicheck(l,e)	vs_assert(e)

#define api_check(l,e,msg)	vsi_apicheck(l,(e) && msg)


// 避免未使用的参数警告的宏
#define UNUSED(x)	((void)(x))


// 显式标注出来代码里进行类型转换的地方
#define cast(t, exp)	((t)(exp))

#define cast_void(i)	cast(void, (i))
#define cast_byte(i)	cast(lu_byte, (i))
#define cast_num(i)	cast(vs_Number, (i))
#define cast_int(i)	cast(int, (i))
#define cast_uchar(i)	cast(unsigned char, (i))


/* cast a signed vs_Integer to vs_Unsigned */
#define l_castS2U(i)	((vs_Unsigned)(i))

/*
** cast a vs_Unsigned to a signed vs_Integer; this cast is
** not strict ISO C, but two-complement architectures should
** work fine.
*/
#define l_castU2S(i)	((vs_Integer)(i))


/*
** non-return type
*/
#if defined(__GNUC__)
#define l_noret		void __attribute__((noreturn))
#elif defined(_MSC_VER) && _MSC_VER >= 1200
#define l_noret		void __declspec(noreturn)
#else
#define l_noret		void
#endif



// C语言调用的最大深度
#if !defined(VSI_MAXCCALLS)
#define VSI_MAXCCALLS		200
#endif



// 虚拟机指令类型 使用至少32位无符号整形来表示
#if VSI_BITSINT >= 32
typedef unsigned int Instruction;
#else
typedef unsigned long Instruction;
#endif



// 短字符串的长度限定
// 小于这个长度的字符串在vs虚拟机内只存一份, 不断访问会被复用
#define VSI_MAXSHORTLEN	40


// 短字符串的哈希表的桶的个数
#define MINSTRTABSIZE	128


// 也是用于字符串缓存
#define STRCACHE_N		53
#define STRCACHE_M		2


// 字符串缓存的最小值
// 词法缓存的最小值
#define VS_MINBUFFER	32


// 整除
#define vsi_numidiv(L,a,b)     ((void)L, l_floor(vsi_numdiv(L,a,b)))

// 浮点数除法
#define vsi_numdiv(L,a,b)      ((a)/(b))

/* floor向下取整, ceil向上取整, trunc是舍尾取整
 * vsi_nummod展开结果
 * { (m) = fmod(a,b); if ((m)*(b) < 0) (m) += (b); }
 * fmod是a/b的余数
 * 关于求模和求余
 * 求模在计算a/b时向负无穷靠近 求余在计算a/b时向0靠近
 * vs中%是求模 c中%是求余 c语言中fmod与%都是求余 fmod可以计算浮点数
 * 例如计算 4%-3
 * 求模 4/-3 得-2(向负无穷) 最终结果 4-(-3*-2) 为-2
 * 求余 4/-3 得-1(向0) 最终结果 4-(-3*-1) 为 1
 * 可以发现求模与求余只有在a,b异号时结果不同 且求模结果 = 求余结果 + b
 * */
// 先使用fmod计算求余结果 如果a,b异号 将求余结果加上b得到求模结果
#define vsi_nummod(L,a,b,m)  \
  { (m) = l_mathop(fmod)(a,b); if ((m)*(b) < 0) (m) += (b); }

// 求幂
#define vsi_numpow(L,a,b)      ((void)L, l_mathop(pow)(a,b))

// 加减乘
#define vsi_numadd(L,a,b)      ((a)+(b))
#define vsi_numsub(L,a,b)      ((a)-(b))
#define vsi_nummul(L,a,b)      ((a)*(b))
// unm -- unary minus -- 一元减
#define vsi_numunm(L,a)        (-(a))
#define vsi_numeq(a,b)         ((a)==(b))
#define vsi_numlt(a,b)         ((a)<(b))
#define vsi_numle(a,b)         ((a)<=(b))
// nan会在0/0的时候出现
#define vsi_numisnan(a)        (!vsi_numeq((a), (a)))

#endif
