
#ifndef vsconf_h
#define vsconf_h

#include <limits.h>
#include <stddef.h>

/*
** By default, Lua on Windows use (some) specific Windows features
*/
#if !defined(VS_USE_C89) && defined(_WIN32) && !defined(_WIN32_WCE)
#define VS_USE_WINDOWS  /* enable goodies for regular Windows */
#endif


#if defined(VS_USE_WINDOWS)
#define VS_DL_DLL	/* enable support for DLL */
#define VS_USE_C89	/* broadly, Windows is C89 */
#endif


#if defined(VS_USE_LINUX)
#define VS_USE_POSIX
#define VS_USE_DLOPEN		/* needs an extra library: -ldl */
#define VS_USE_READLINE	/* needs some extra libraries */
#endif


// 这个宏在编译的时候使用-D指定
#if defined(VS_USE_MACOSX)
#define VS_USE_POSIX
#define VS_USE_DLOPEN		/* MacOS does not need -ldl */
#define VS_USE_READLINE	/* needs an extra library: -lreadline */
#endif


/*
@@ VS_C89_NUMBERS ensures that Lua uses the largest types available for
** C89 ('long' and 'double'); Windows always has '__int64', so it does
** not need to use this case.
*/
#if defined(VS_USE_C89) && !defined(VS_USE_WINDOWS)
#define VS_C89_NUMBERS
#endif



/*
@@ VSI_BITSINT defines the (minimum) number of bits in an 'int'.
*/
/* avoid undefined shifts */
// 16 0111 1111 1111 1111 处理后是0
// 32 0111 1111 1111 1111 1111 1111 1111 1111 处理后是1
// 分两次移位的原因是如果是16直接移位30次会越界(shift count >= width of type)
#if ((INT_MAX >> 15) >> 15) >= 1
#define VSI_BITSINT	32
#else
/* 'int' always must have at least 16 bits */
#define VSI_BITSINT	16
#endif


/*
@@ VS_INT_TYPE defines the type for Lua integers.
@@ VS_FLOAT_TYPE defines the type for Lua floats.
** Lua should work fine with any mix of these options (if supported
** by your C compiler). The usual configurations are 64-bit integers
** and 'double' (the default), 32-bit integers and 'float' (for
** restricted platforms), and 'long'/'double' (for C compilers not
** compliant with C99, which may not have support for 'long long').
*/

/* predefined options for VS_INT_TYPE */
#define VS_INT_INT		1
#define VS_INT_LONG		2
#define VS_INT_LONGLONG	3

/* predefined options for VS_FLOAT_TYPE */
#define VS_FLOAT_FLOAT		1
#define VS_FLOAT_DOUBLE	2
#define VS_FLOAT_LONGDOUBLE	3

// 没有定义 VS_32BITS
#if defined(VS_32BITS)		/* { */
/*
** 32-bit integers and 'float'
*/
#if VSI_BITSINT >= 32  /* use 'int' if big enough */
#define VS_INT_TYPE	VS_INT_INT
#else  /* otherwise use 'long' */
#define VS_INT_TYPE	VS_INT_LONG
#endif
#define VS_FLOAT_TYPE	VS_FLOAT_FLOAT

#elif defined(VS_C89_NUMBERS)	/* }{ */
/*
** largest types available for C89 ('long' and 'double')
*/
#define VS_INT_TYPE	VS_INT_LONG
#define VS_FLOAT_TYPE	VS_FLOAT_DOUBLE

#endif				/* } */


/*
** default configuration for 64-bit Lua ('long long' and 'double')
*/
// 正常的64位系统vs的int类型是long long类型, float类型是double
// VS_FLOAT_TYPE目前没有可能是VS_FLOAT_LONGDOUBLE
#if !defined(VS_INT_TYPE)
#define VS_INT_TYPE	VS_INT_LONGLONG
#endif

#if !defined(VS_FLOAT_TYPE)
#define VS_FLOAT_TYPE	VS_FLOAT_DOUBLE
#endif

/* }================================================================== */




/*
** {==================================================================
** Configuration for Paths.
** ===================================================================
*/

/*
** VS_PATH_SEP is the character that separates templates in a path.
** VS_PATH_MARK is the string that marks the substitution points in a
** template.
** VS_EXEC_DIR in a Windows path is replaced by the executable's
** directory.
*/
#define VS_PATH_SEP            ";"
#define VS_PATH_MARK           "?"
#define VS_EXEC_DIR            "!"


/*
@@ VS_PATH_DEFAULT is the default path that Lua uses to look for
** Lua libraries.
@@ VS_CPATH_DEFAULT is the default path that Lua uses to look for
** C libraries.
** CHANGE them if your machine has a non-conventional directory
** hierarchy or if you want to install your libraries in
** non-conventional directories.
*/
#define VS_VDIR	VS_VERSION_MAJOR "." VS_VERSION_MINOR // 5.3
#if defined(_WIN32)	/* { */
/*
** In Windows, any exclamation mark ('!') in the path is replaced by the
** path of the directory of the executable file of the current process.
*/
#define VS_LDIR	"!\\vs\\"
#define VS_CDIR	"!\\"
#define VS_SHRDIR	"!\\..\\share\\vs\\" VS_VDIR "\\"
#define VS_PATH_DEFAULT  \
		VS_LDIR"?.vs;"  VS_LDIR"?\\init.vs;" \
		VS_CDIR"?.vs;"  VS_CDIR"?\\init.vs;" \
		VS_SHRDIR"?.vs;" VS_SHRDIR"?\\init.vs;" \
		".\\?.vs;" ".\\?\\init.vs"
#define VS_CPATH_DEFAULT \
		VS_CDIR"?.dll;" \
		VS_CDIR"..\\lib\\vs\\" VS_VDIR "\\?.dll;" \
		VS_CDIR"loadall.dll;" ".\\?.dll"

#else			/* }{ */

#define VS_ROOT	"/usr/local/" // /usr/local/
#define VS_LDIR	VS_ROOT "share/vs/" VS_VDIR "/" // /usr/local/share/vs/5.3/
#define VS_CDIR	VS_ROOT "lib/vs/" VS_VDIR "/" // /usr/local/lib/vs/5.3/
// vs中执行 print(package.path)可以看到 VS_PATH_DEFAULT
// VS_PATH_DEFAULT 是vs模块的默认搜索路径
// /usr/local/share/vs/5.3/?.vs;/usr/local/share/vs/5.3/?/init.vs;/usr/local/lib/vs/5.3/?.vs;/usr/local/lib/vs/5.3/?/init.vs;./?.vs;./?/init.vs
#define VS_PATH_DEFAULT  \
		VS_LDIR "?.vs;"  VS_LDIR "?/init.vs;" \
		VS_CDIR "?.vs;"  VS_CDIR "?/init.vs;" \
		"./?.vs;" "./?/init.vs"
// 找不到与模块名一致的vs文件, 开始搜索c程序库
// /usr/local/lib/vs/5.3/?.so;/usr/local/lib/vs/5.3/loadall.so;./?.so
#define VS_CPATH_DEFAULT \
		VS_CDIR"?.so;" VS_CDIR"loadall.so;" "./?.so"
#endif			/* } */


/*
@@ VS_DIRSEP is the directory separator (for submodules).
** CHANGE it if your machine does not use "/" as the directory separator
** and is not Windows. (On Windows Lua automatically uses "\".)
*/
// 路径分割符
#if defined(_WIN32)
#define VS_DIRSEP	"\\"
#else
#define VS_DIRSEP	"/"
#endif

/* }================================================================== */


/*
** {==================================================================
** Marks for exported symbols in the C code
** ===================================================================
*/

/*
@@ VS_API is a mark for all core API functions.
@@ VSLIB_API is a mark for all auxiliary library functions.
@@ VSMOD_API is a mark for all standard library opening functions.
** CHANGE them if you need to define those functions in some special way.
** For instance, if you want to create one Windows DLL with the core and
** the libraries, you may want to use the following definition (define
** VS_BUILD_AS_DLL to get it).
*/
#if defined(VS_BUILD_AS_DLL)	/* { */

#if defined(VS_CORE) || defined(VS_LIB)	/* { */
#define VS_API __declspec(dllexport)
#else						/* }{ */
#define VS_API __declspec(dllimport)
#endif						/* } */

#else				/* }{ */

// 使用externa标记可以不引用头文件编译
#define VS_API		extern

#endif				/* } */


/* more often than not the libs go together with the core */
#define VSLIB_API	VS_API
#define VSMOD_API	VSLIB_API


/*
@@ VSI_FUNC is a mark for all extern functions that are not to be
** exported to outside modules.
@@ VSI_DDEF and VSI_DDEC are marks for all extern (const) variables
** that are not to be exported to outside modules (VSI_DDEF for
** definitions and VSI_DDEC for declarations).
** CHANGE them if you need to mark them in some special way. Elf/gcc
** (versions 3.2 and later) mark them as "hidden" to optimize access
** when Lua is compiled as a shared library. Not all elf targets support
** this attribute. Unfortunately, gcc does not offer a way to check
** whether the target offers that support, and those without support
** give a warning about it. To avoid these warnings, change to the
** default definition.
*/
#define VSI_FUNC	extern

// DEC declarations
// 在声明时使用
#define VSI_DDEC	VSI_FUNC
// DEF definitions
// 在定义时使用
#define VSI_DDEF	/* empty */

/* }================================================================== */


/*
** {==================================================================
** Configuration for Numbers.
** Change these definitions if no predefined VS_FLOAT_* / VS_INT_*
** satisfy your needs.
** ===================================================================
*/

/*
@@ VS_NUMBER is the floating-point type used by Lua.
@@ VSI_UACNUMBER is the result of a 'default argument promotion'
@@ over a floating number.
@@ l_mathlim(x) corrects limit name 'x' to the proper float type
** by prefixing it with one of FLT/DBL/LDBL.
@@ VS_NUMBER_FRMLEN is the length modifier for writing floats.
@@ VS_NUMBER_FMT is the format for writing floats.
@@ vs_number2str converts a float to a string.
@@ l_mathop allows the addition of an 'l' or 'f' to all math operations.
@@ l_floor takes the floor of a float.
@@ vs_str2number converts a decimal numeric string to a number.
*/


/* The following definitions are good for most cases here */
// l_floor(10) 展开后是 (floor(10))
// 一般情况下 l_mathop(x) 什么作用都没有就是x
// 如果修改了VS_FLOAT_TYPE(默认为double) 那么标准库内的函数就要修改
// 对fabs来说 如果fabsl(long double) fabsf(float)

#define l_floor(x)		(l_mathop(floor)(x))

// 转换结果 snprintf((s),sz,"%.14g",(double)(n))
// s是字符串 sz是字符串长度
#define vs_number2str(s,sz,n)  \
	l_sprintf((s), sz, VS_NUMBER_FMT, (VSI_UACNUMBER)(n))

/*
@@ vs_numbertointeger converts a float number to an integer, or
** returns 0 if float is not within the range of a vs_Integer.
** (The range comparisons are tricky because of rounding. The tests
** here assume a two-complement representation, where MININTEGER always
** has an exact representation as a float; MAXINTEGER may not have one,
** and therefore its conversion to float may have an ill-defined value.)
*/

// VS_MININTEGER INT_MIN
// ((n) >= (double)(INT_MIN) && (n) < -(double)(INT_MIN) && (*(p) = (long long)(n), 1))
// 检测n在范围内, 返回值是转换是否成功
// 将指针p的值修改为n
#define vs_numbertointeger(n,p) \
  ((n) >= (VS_NUMBER)(VS_MININTEGER) && \
   (n) < -(VS_NUMBER)(VS_MININTEGER) && \
      (*(p) = (VS_INTEGER)(n), 1))


/* now the variable definitions */

#if VS_FLOAT_TYPE == VS_FLOAT_FLOAT		/* { single float */

#define VS_NUMBER	float

#define l_mathlim(n)		(FLT_##n)

#define VSI_UACNUMBER	double

#define VS_NUMBER_FRMLEN	""
#define VS_NUMBER_FMT		"%.7g"

#define l_mathop(op)		op##f

#define vs_str2number(s,p)	strtof((s), (p))


#elif VS_FLOAT_TYPE == VS_FLOAT_LONGDOUBLE	/* }{ long double */

#define VS_NUMBER	long double

#define l_mathlim(n)		(LDBL_##n)

#define VSI_UACNUMBER	long double

#define VS_NUMBER_FRMLEN	"L"
#define VS_NUMBER_FMT		"%.19Lg"

#define l_mathop(op)		op##l

#define vs_str2number(s,p)	strtold((s), (p))

#elif VS_FLOAT_TYPE == VS_FLOAT_DOUBLE	/* }{ double */

#define VS_NUMBER	double

// 在传入参数前加上DBL
#define l_mathlim(n)		(DBL_##n)

#define VSI_UACNUMBER	double

#define VS_NUMBER_FRMLEN	""
// 数值对于类型说明符g或G，表示可输出的最大有效数字
// 这里意思是最大有效数字是14位
#define VS_NUMBER_FMT		"%.14g"

// 默认情况下 l_mathop 不做任何操作
#define l_mathop(op)		op

#define vs_str2number(s,p)	strtod((s), (p))

#else						/* }{ */

#error "numeric float type not defined"

#endif					/* } */



/*
@@ VS_INTEGER is the integer type used by Lua.
**
@@ VS_UNSIGNED is the unsigned version of VS_INTEGER.
**
@@ VSI_UACINT is the result of a 'default argument promotion'
@@ over a lUA_INTEGER.
@@ VS_INTEGER_FRMLEN is the length modifier for reading/writing integers.
@@ VS_INTEGER_FMT is the format for writing integers.
@@ VS_MAXINTEGER is the maximum value for a VS_INTEGER.
@@ VS_MININTEGER is the minimum value for a VS_INTEGER.
@@ vs_integer2str converts an integer to a string.
*/


/* The following definitions are good for most cases here */

// %d=int
// %ld=long
// %lld=long long
// 这里VS_INTEGER_FMT解析成 "%" "ll" "d"

#define VS_INTEGER_FMT		"%" VS_INTEGER_FRMLEN "d"

// VSI_UACINT是VS_INTEGER默认参数提升的结果
#define VSI_UACINT		VS_INTEGER

#define vs_integer2str(s,sz,n)  \
	l_sprintf((s), sz, VS_INTEGER_FMT, (VSI_UACINT)(n))

/*
** use VSI_UACINT here to avoid problems with promotions (which
** can turn a comparison between unsigneds into a signed comparison)
*/
#define VS_UNSIGNED		unsigned VSI_UACINT


/* now the variable definitions */

#if VS_INT_TYPE == VS_INT_INT		/* { int */

#define VS_INTEGER		int
#define VS_INTEGER_FRMLEN	""

#define VS_MAXINTEGER		INT_MAX
#define VS_MININTEGER		INT_MIN

#elif VS_INT_TYPE == VS_INT_LONG	/* }{ long */

#define VS_INTEGER		long
#define VS_INTEGER_FRMLEN	"l"

#define VS_MAXINTEGER		LONG_MAX
#define VS_MININTEGER		LONG_MIN

#elif VS_INT_TYPE == VS_INT_LONGLONG	/* }{ long long */

/* use presence of macro LLONG_MAX as proxy for C99 compliance */
#if defined(LLONG_MAX)		/* { */
/* use ISO C99 stuff */

#define VS_INTEGER		long long
#define VS_INTEGER_FRMLEN	"ll"

#define VS_MAXINTEGER		LLONG_MAX
#define VS_MININTEGER		LLONG_MIN

#elif defined(VS_USE_WINDOWS) /* }{ */
/* in Windows, can use specific Windows types */

#define VS_INTEGER		__int64
#define VS_INTEGER_FRMLEN	"I64"

#define VS_MAXINTEGER		_I64_MAX
#define VS_MININTEGER		_I64_MIN

#else				/* }{ */

#error "Compiler does not support 'long long'. Use option '-DVS_32BITS' \
  or '-DVS_C89_NUMBERS' (see file 'vsconf.h' for details)"

#endif				/* } */

#else				/* }{ */

#error "numeric integer type not defined"

#endif				/* } */

/* }================================================================== */


/*
** {==================================================================
** Dependencies with C99 and other C details
** ===================================================================
*/

/*
@@ l_sprintf is equivalent to 'snprintf' or 'sprintf' in C89.
** (All uses in Lua have only one format item.)
*/
#if !defined(VS_USE_C89)
#define l_sprintf(s,sz,f,i)	snprintf(s,sz,f,i)
#else
#define l_sprintf(s,sz,f,i)	((void)(sz), sprintf(s,f,i))
#endif


/*
@@ vs_strx2number converts an hexadecimal numeric string to a number.
** In C99, 'strtod' does that conversion. Otherwise, you can
** leave 'vs_strx2number' undefined and Lua will provide its own
** implementation.
*/
#if !defined(VS_USE_C89)
#define vs_strx2number(s,p)		vs_str2number(s,p)
#endif


/*
@@ vs_number2strx converts a float to an hexadecimal numeric string.
** In C99, 'sprintf' (with format specifiers '%a'/'%A') does that.
** Otherwise, you can leave 'vs_number2strx' undefined and Lua will
** provide its own implementation.
*/
#if !defined(VS_USE_C89)
#define vs_number2strx(L,b,sz,f,n)  \
	((void)L, l_sprintf(b,sz,f,(VSI_UACNUMBER)(n)))
#endif


/*
** 'strtof' and 'opf' variants for math functions are not valid in
** C89. Otherwise, the macro 'HUGE_VALF' is a good proxy for testing the
** availability of these variants. ('math.h' is already included in
** all files that use these macros.)
*/
#if defined(VS_USE_C89) || (defined(HUGE_VAL) && !defined(HUGE_VALF))
#undef l_mathop  /* variants not available */
#undef vs_str2number
#define l_mathop(op)		(vs_Number)op  /* no variant */
#define vs_str2number(s,p)	((vs_Number)strtod((s), (p)))
#endif



/*
@@ vs_getlocaledecpoint gets the locale "radix character" (decimal point).
** Change that if you do not want to use C locales. (Code using this
** macro must include header 'locale.h'.)
*/
// decpoint decimal_point 小数点
// vs_getlocaledecpoint 得到结果 .
#define vs_getlocaledecpoint()		(localeconv()->decimal_point[0])

/* }================================================================== */


/*
** {==================================================================
** Language Variations
** =====================================================================
*/

/*
@@ VS_USE_APICHECK turns on several consistency checks on the C API.
** Define it as a help when debugging C code.
*/
#if defined(VS_USE_APICHECK)
#include <assert.h>
#define vsi_apicheck(l,e)	assert(e)
#endif

/* }================================================================== */


/*
** {==================================================================
** Macros that affect the API and must be stable (that is, must be the
** same when you compile Lua and when you compile code that links to
** Lua). You probably do not want/need to change them.
** =====================================================================
*/

/*
@@ VSI_MAXSTACK limits the size of the Lua stack.
** CHANGE it if you need a different limit. This limit is arbitrary;
** its only purpose is to stop Lua from consuming unlimited stack
** space (and to reserve some numbers for pseudo-indices).
*/
// VSI_MAXSTACK vs调用栈的最大深度, 超过这个深度将报错
#if VSI_BITSINT >= 32
#define VSI_MAXSTACK		1000000
#else
#define VSI_MAXSTACK		15000
#endif


/*
@@ VS_EXTRASPACE defines the size of a raw memory area associated with
** a Lua state with very fast access.
** CHANGE it if you need a different size.
*/
// 分配给vs虚拟机的额外空间
#define VS_EXTRASPACE		(sizeof(void *))


/*
@@ VS_IDSIZE gives the maximum size for the description of the source
@@ of a function in debug information.
** CHANGE it if you want a different size.
*/
// 调试时输出的有关函数来源的描述的最大长度
#define VS_IDSIZE	60


/*
@@ VSL_BUFFERSIZE is the buffer size used by the lauxlib buffer system.
** CHANGE it if it uses too much C-stack space. (For long double,
** 'string.format("%.99f", -1e4932)' needs 5034 bytes, so a
** smaller buffer would force a memory allocation for each call to
** 'string.format'.)
*/
#if VS_FLOAT_TYPE == VS_FLOAT_LONGDOUBLE
#define VSL_BUFFERSIZE		8192
#else
#define VSL_BUFFERSIZE   ((int)(0x80 * sizeof(void*) * sizeof(vs_Integer)))
#endif

/* }================================================================== */


/*
@@ VS_QL describes how error messages quote program elements.
** Lua does not use these macros anymore; they are here for
** compatibility only.
*/
#define VS_QL(x)	"'" x "'"
#define VS_QS		VS_QL("%s")




/* =================================================================== */

/*
** Local configuration. You can use this space to add your redefinitions
** without modifying the main part of the file.
*/





#endif

