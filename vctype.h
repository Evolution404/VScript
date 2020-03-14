#ifndef vctype_h
#define vctype_h

#include <ctype.h>


#define lislalpha(c)	(isalpha(c) || (c) == '_')  // 大小写字母或_
#define lislalnum(c)	(isalnum(c) || (c) == '_')  // 大小写字母 数字或_
#define lisdigit(c)	(isdigit(c))  // 数字
/*
 * 空白符
 * ' '	  0x20	空格 (SPC)
 * '\t'	0x09	水平制表符 (TAB)
 * '\n'	0x0a	换行符 (LF)
 * '\v'	0x0b	垂直制表符 (VT)
 * '\f'	0x0c	换页 (FF)
 * '\r'	0x0d	回车 (CR)
*/
#define lisspace(c)	(isspace(c))  // 是否空白符
#define lisprint(c)	(isprint(c))  // 是否可打印字符
#define lisxdigit(c)	(isxdigit(c))  // 是否16进制数字 0-9 和 A-F(或a-f)

#define ltolower(c)	(tolower(c))  // 转换成小写
#endif
