#ifndef vstring_h
#define vstring_h

#include "vgc.h"
#include "vobject.h"
#include "vstate.h"


// TString对象需要的空间是结构体需要空间+字符串长度+1(存放\0)
// l是存放字符串的长度(不包括\0)
#define sizelstring(l)  (sizeof(union UTString) + ((l) + 1) * sizeof(char))

// Udata对象需要的空间包括 结构体占用空间+存放数据的大小
// l是Udata存放数据的大小
#define sizeludata(l)	(sizeof(union UUdata) + (l))
// u是Udata对象 计算该对象总共占用的空间 包括了实际存放的数据占用的空间
#define sizeudata(u)	sizeludata((u)->len)

// 用于创建一个明确文本的字符串 例如 vsS_newliteral(L, "hello")
#define vsS_newliteral(L, s)	(vsS_newlstr(L, "" s, \
                                 (sizeof(s)/sizeof(char))-1))


/*
** test whether a string is a reserved word
*/
// 检查一个TString对象是否是关键字
// 是短字符串且extra不为0 说明是关键字
#define isreserved(s)	((s)->tt == VS_TSHRSTR && (s)->extra > 0)


/*
** equality for short strings, which are always internalized
*/
// 比较短字符串是否相等
// a,b是TString对象 短字符串都被内化到虚拟机内 字符串相等一定地址相等 只需要比较地址
#define eqshrstr(a,b)	check_exp((a)->tt == VS_TSHRSTR, (a) == (b))


VSI_FUNC unsigned int vsS_hash (const char *str, size_t l, unsigned int seed);
VSI_FUNC unsigned int vsS_hashlongstr (TString *ts);
VSI_FUNC int vsS_eqlngstr (TString *a, TString *b);
VSI_FUNC void vsS_resize (vs_State *L, int newsize);
VSI_FUNC void vsS_clearcache (global_State *g);
VSI_FUNC void vsS_init (vs_State *L);
VSI_FUNC void vsS_remove (vs_State *L, TString *ts);
VSI_FUNC Udata *vsS_newudata (vs_State *L, size_t s);
VSI_FUNC TString *vsS_newlstr (vs_State *L, const char *str, size_t l);
VSI_FUNC TString *vsS_new (vs_State *L, const char *str);
VSI_FUNC TString *vsS_createlngstrobj (vs_State *L, size_t l);


#endif
