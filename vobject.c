#define lobject_c

#include <locale.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vs.h"
#include "vobject.h"
#include "vvm.h"
#include "vstring.h"
#include "vdebug.h"
#include "vctype.h"
#include "vdo.h"


// VSI_DDEF目前就是空 全局常量在定义时的标记
// 结构体初始化 {NULL} 会自动让结构体的所有字段都是NULL也就是0
// const TValue vsO_nilobject_ = {{NULL}, 0};
// 也就是初始化TValue的value_字段为{NULL}, tt_字段为0
VSI_DDEF const TValue vsO_nilobject_ = {NILCONSTANT};


/*
** converts an integer to a "floating point byte", represented as
** (eeeeexxx), where the real value is (1xxx) * 2^(eeeee - 1) if
** eeeee != 0 and (xxx) otherwise.
*/
// 使用浮点数的方法扩充整数范围
// 最后三位是基数 剩余位数是指数位
int vsO_int2fb (unsigned int x) {
  int e = 0;  /* exponent 指数部分 */ 
  // 三位基数可以表示到7 指数位为0 直接返回
  if (x < 8) return x;
  // 先一次移动4位
  // 8<<4 00000000 00000000 00000000 10000000
  while (x >= (8 << 4)) {  /* coarse steps */
    x = (x + 0xf) >> 4;  /* x = ceil(x / 16) 基数位除16 并向上取整 */
    // 经过测试这样性能提升不明显 也许是被编译器优化了
    e += 4; // 指数位加4 相当于乘16
  }
  // 一次移动1位
  while (x >= (8 << 1)) {  /* fine steps */
    x = (x + 1) >> 1;  /* x = ceil(x / 2) */
    e++;
  }
  return ((e+1) << 3) | (cast_int(x) - 8);
}


/* converts back */
// 上面函数的反函数, 逆向操作
int vsO_fb2int (int x) {
  return (x < 8) ? x : ((x & 7) + 8) << ((x >> 3) - 1);
}


/*
** Computes ceil(log2(x))
*/
// 求一个数的2的对数并向上取整
int vsO_ceillog2 (unsigned int x) {
  static const lu_byte log_2[256] = {  /* log_2[i] = ceil(log2(i + 1)) */
    0,1,2,2,3,3,3,3,4,4,4,4,4,4,4,4,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
    6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8
  };
  int l = 0;
  // 小于等于256的数字可以直接从数组中取出结果 计算ceil(log2(17)) 取log_2[16]即可
  // 大于256的数字 ceil(log2(x)) = ceil(log2((x-1)//256+1)) + 8
  // 例如512是ceil(log2(2))+8    513是ceil(log2(3))+8 所以结果前者是9后者是10
  x--;
  while (x >= 256) { l += 8; x >>= 8; }
  return l + log_2[x];
}


// arith arithmetic vs的算术运算
// VS_OPADD: 加法 (+)
// VS_OPSUB: 减法 (-)
// VS_OPMUL: 乘法 (*)
// VS_OPDIV: 浮点除法 (/)
// VS_OPIDIV: 向下取整的除法 (//)
// VS_OPMOD: 取模 (%)
// VS_OPPOW: 乘方 (^)
// VS_OPUNM: 取负 (一元 -)
// VS_OPBNOT: 按位取反 (~)
// VS_OPBAND: 按位与 (&)
// VS_OPBOR: 按位或 (|)
// VS_OPBXOR: 按位异或 (~)
// VS_OPSHL: 左移 (<<)
// VS_OPSHR: 右移 (>>)

// 关于整数的算术运算
static vs_Integer intarith (vs_State *L, int op, vs_Integer v1,
                                                   vs_Integer v2) {
  // intop(+,v1,v2)  ((vs_Integer)(((vs_Unsigned)(v1)) + ((vs_Unsigned)(v2))))
  switch (op) {
    case VS_OPADD: return intop(+, v1, v2);
    case VS_OPSUB:return intop(-, v1, v2);
    case VS_OPMUL:return intop(*, v1, v2);
    case VS_OPMOD: return vsV_mod(L, v1, v2);
    case VS_OPIDIV: return vsV_div(L, v1, v2);
    case VS_OPBAND: return intop(&, v1, v2);
    case VS_OPBOR: return intop(|, v1, v2);
    case VS_OPBXOR: return intop(^, v1, v2);
    case VS_OPSHL: return vsV_shiftl(v1, v2);          // 左移
    case VS_OPSHR: return vsV_shiftl(v1, -v2);         // 右移
    case VS_OPUNM: return intop(-, 0, v1);              // 取负运算,传入v2是假操作数,直接让v1取负就行
    case VS_OPBNOT: return intop(^, ~l_castS2U(0), v1); // 与全1进行 异或 实现按位取反
    default: vs_assert(0); return 0;
  }
}


// 关于浮点数的算术运算
static vs_Number numarith (vs_State *L, int op, vs_Number v1,
                                                  vs_Number v2) {
  switch (op) {
    case VS_OPADD: return vsi_numadd(L, v1, v2);
    case VS_OPSUB: return vsi_numsub(L, v1, v2);
    case VS_OPMUL: return vsi_nummul(L, v1, v2);
    case VS_OPDIV: return vsi_numdiv(L, v1, v2);
    case VS_OPPOW: return vsi_numpow(L, v1, v2);
    case VS_OPIDIV: return vsi_numidiv(L, v1, v2); // 整除的实现是 floor(v1/v2)
    case VS_OPUNM: return vsi_numunm(L, v1);
    case VS_OPMOD: {
      vs_Number m;
      vsi_nummod(L, v1, v2, m);
      return m;
    }
    default: vs_assert(0); return 0;
  }
}


// 通用的算术运算函数 传入TValue类型
void vsO_arith (vs_State *L, int op, const TValue *p1, const TValue *p2,
                 TValue *res) {
  switch (op) {
    case VS_OPBAND: case VS_OPBOR: case VS_OPBXOR:
    case VS_OPSHL: case VS_OPSHR:
    case VS_OPBNOT: {  /* operate only on integers 只能对整数进行的操作 */
      vs_Integer i1; vs_Integer i2;
      vs_Number dummy;
      if (tointeger(p1, &i1) && tointeger(p2, &i2)) {  // 确保p1和p2都是整数
        setivalue(res, intarith(L, op, i1, i2));  // 计算结果并对res赋值
        return;
      }
      else if (tonumber(p1, &dummy) && tonumber(p2, &dummy)) // 都是数字类型 但不都是整数
        vsG_tointerror(L, p1, p2);
      else
        vsG_opinterror(L, p1, p2, "perform bitwise operation on");
    }
    case VS_OPDIV: case VS_OPPOW: {  /* operate only on floats 只能对浮点数进行的操作 */
      vs_Number n1; vs_Number n2;
      if (tonumber(p1, &n1) && tonumber(p2, &n2)) {  // 确保两者都是浮点型
        setfltvalue(res, numarith(L, op, n1, n2));
        return;
      }
      else break;  /* go to the end */
    }
    default: {  /* other operations 其他的运算符 */
      vs_Number n1; vs_Number n2;
      if (ttisinteger(p1) && ttisinteger(p2)) {  // 都是整数
        setivalue(res, intarith(L, op, ivalue(p1), ivalue(p2)));
        return;
      }
      else if (tonumber(p1, &n1) && tonumber(p2, &n2)) {  // 都是浮点数
        setfltvalue(res, numarith(L, op, n1, n2));
        return;
      }
      else
        vsG_opinterror(L, p1, p2, "perform arithmetic on");
    }
  }
}


// 计算16进制数字的值 例如a就是10 f是15
int vsO_hexavalue (int c) {
  // 字符0-9 返回对应数字
  if (lisdigit(c)) return c - '0';
  // a-f 返回10-15
  else return (ltolower(c) - 'a') + 10;
}


// 判断是否是负数 返回值 负数1 正数0
// 副作用 将字符串指向第一个数字 去除正负号(+-)
static int isneg (const char **s) {
  if (**s == '-') { (*s)++; return 1; }
  else if (**s == '+') (*s)++;
  return 0;
}

/* maximum length of a numeral */
// 数字的最大长度
#if !defined (L_MAXLENNUM)
#define L_MAXLENNUM	200
#endif

// 返回 \0 转换成功  NULL转换失败或转换完成有拖尾字符
// mode为x代表转换16进制数 在使用strtod的情况下不区分mode也不影响
static const char *l_str2dloc (const char *s, vs_Number *result, int mode) {
  char *endptr;
  // 检查mode x使用16进制转换 0的话10进制转换
  *result = (mode == 'x') ? vs_strx2number(s, &endptr)  /* try to convert */
                          : vs_str2number(s, &endptr);
  if (endptr == s) return NULL;  /* nothing recognized? 没有一个字符可以转换直接返回*/
  while (lisspace(cast_uchar(*endptr))) endptr++;  /* skip trailing spaces 查看转换后是否有空白字符 跳过所有空白字符 */
  // endptr指向了\0 返回\0 否则返回NULL
  return (*endptr == '\0') ? endptr : NULL;  /* OK if no trailing characters */
}


/*
** Convert string 's' to a Lua number (put in 'result'). Return NULL
** on fail or the address of the ending '\0' on success.
** 'pmode' points to (and 'mode' contains) special things in the string:
** - 'x'/'X' means an hexadecimal numeral
** - 'n'/'N' means 'inf' or 'nan' (which should be rejected)
** - '.' just optimizes the search for the common case (nothing special)
** This function accepts both the current locale or a dot as the radix
** mark. If the convertion fails, it may mean number has a dot but
** locale accepts something else. In that case, the code copies 's'
** to a buffer (because 's' is read-only), changes the dot to the
** current locale radix mark, and tries to convert again.
*/
// 将字符串转换为数字 返回转换字符串最后\0的指针 失败返回NULL
// 特性: 跳过字符串开头结尾的空白字符 修正本地小数点不是. 但是错误输入.的错误
static const char *l_str2d (const char *s, vs_Number *result) {
  const char *endptr;
  // 在s中搜索.xXnN 找到.xXnN中任何一个字符立刻返回位置
  // 如果有n或N就被认为是inf或nan 拒绝转换
  const char *pmode = strpbrk(s, ".xXnN");
  // mode 可能为x,n,0
  // x 认为是16进制数
  // 0 正常模式 既不是16进制也不是无穷
  int mode = pmode ? ltolower(cast_uchar(*pmode)) : 0;
  if (mode == 'n')  /* reject 'inf' and 'nan' inf或nan直接拒绝 */
    return NULL;
  endptr = l_str2dloc(s, result, mode);  /* try to convert */
  // 直接进行转换返回了NULL说明了转换失败
  // 有可能是本地小数点是逗号 而数字使用了.当做小数点 替换.为,再次尝试进行转换
  // 有可能是转换字符串过长 检测出直接报错
  if (endptr == NULL) {  /* failed? may be a different locale */
    char buff[L_MAXLENNUM + 1];
    const char *pdot = strchr(s, '.');
    if (strlen(s) > L_MAXLENNUM || pdot == NULL)  // 长度过长 或者 没有找到.都报错
      return NULL;  /* string too long or no dot; fail */
    // 处理错误输入了小数点
    strcpy(buff, s);  /* copy string to buffer */
    buff[pdot - s] = vs_getlocaledecpoint();  /* correct decimal point 修正.为本地小数点 */
    endptr = l_str2dloc(buff, result, mode);  /* try again */
    // 这里返回的endptr是相对于buff的位置, 下面修正为s的位置
    if (endptr != NULL)
      endptr = s + (endptr - buff);  /* make relative to 's' */
  }
  return endptr;
}


// 最大整数除10
#define MAXBY10		cast(vs_Unsigned, VS_MAXINTEGER / 10)
// 最大整数的最后一位
#define MAXLASTD	cast_int(VS_MAXINTEGER % 10)

// 将s转换为整数放入result中
// 返回s的'\0'的位置 转换失败返回NULL
static const char *l_str2int (const char *s, vs_Integer *result) {
  vs_Unsigned a = 0;
  int empty = 1;
  int neg;
  while (lisspace(cast_uchar(*s))) s++;  /* skip initial spaces 跳过字符串开头的空白字符 */
  neg = isneg(&s);  // 如果开头有正负号 跳过
  if (s[0] == '0' &&  // s是16进制数
      (s[1] == 'x' || s[1] == 'X')) {  /* hex? */
    s += 2;  /* skip '0x' */
    // 将16进制的s转换成10进制放入a中
    // isxdigit 0-f之间返回非0,否则返回0
    for (; lisxdigit(cast_uchar(*s)); s++) {
      a = a * 16 + vsO_hexavalue(*s);  // 原来的a*16加上当前位的16进制数在10进制表达的数
      empty = 0;
    }
  }
  else {  /* decimal */
    for (; lisdigit(cast_uchar(*s)); s++) {
      int d = *s - '0';
      // 检测继续执行是否会导致超过long long的最大值
      if (a >= MAXBY10 && (a > MAXBY10 || d > MAXLASTD + neg))  /* overflow? */
        return NULL;  /* do not accept it (as integer) */
      a = a * 10 + d;
      empty = 0;
    }
  }
  while (lisspace(cast_uchar(*s))) s++;  /* skip trailing spaces 跳过尾部的空白字符 */
  if (empty || *s != '\0') return NULL;  /* something wrong in the numeral 转换出现了问题 */
  else {
    // 是否是负数,是的话取相反数然后强制类型转换
    // 这里0u没发现意义所在,可能是为了转成无符号数
    *result = l_castU2S((neg) ? 0u - a : a);
    return s;
  }
}


// 将字符串转换成数字放入o中
// 返回转换的字符串的长度(包括\0)
// 可以转换整型或浮点型
size_t vsO_str2num (const char *s, TValue *o) {
  vs_Integer i; vs_Number n;
  const char *e;
  if ((e = l_str2int(s, &i)) != NULL) {  /* try as an integer 尝试转换成整数 */
    setivalue(o, i);
  }
  else if ((e = l_str2d(s, &n)) != NULL) {  /* else try as a float 尝试转换成浮点数 */
    setfltvalue(o, n);
  }
  else
    return 0;  /* conversion failed 转换失败 返回0代表转换的字符个数是0 */
  return (e - s) + 1;  /* success; return string size 返回的是字符串的大小(包括\0) e指向了'\0' */
}
// 将x从Unicode码转换位utf8编码
// 将buff从后往前填充 倒数第n字节是第一个有效字节
// 返回n表示buff的有效字节数
/*
16进制 Unicode      | 二进制 utf8编码
0000 0000-0000 007F | 0xxxxxxx                                      1
0000 0080-0000 07FF | 110xxxxx 10xxxxxx                             2
0000 0800-0000 FFFF | 1110xxxx 10xxxxxx 10xxxxxx                    3
0001 0000-0010 FFFF | 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx           4
*/
int vsO_utf8esc (char *buff, unsigned long x) {
  int n = 1;  /* number of bytes put in buffer (backwards) */
  vs_assert(x <= 0x10FFFF);  // utf8最大编码到10FFFF
  if (x < 0x80)  /* ascii? ascii码字符 直接放到缓存最后一位 */
    // 这里就是上面表格的第一行的情况 需要最前面补零可以省略
    buff[UTF8BUFFSZ - 1] = cast(char, x);
  else {  /* need continuation bytes 不是utf8的一字节字符 */
    unsigned int mfb = 0x3f;  /* maximum that fits in first byte 两字节字符首字节有5位有效位 */
    // do-while循环执行一次之后mfb就只有5位1 所以初始设置位6位1
    do {  /* add continuation bytes */
      // 0x80:1000 0000
      // 0x3f:0011 1111 
      // 与运算取x的后6位 或运算设置前两位10 这样构造出来额外的一字节
      buff[UTF8BUFFSZ - (n++)] = cast(char, 0x80 | (x & 0x3f));
      x >>= 6;  /* remove added bits 一轮使用x的6位 */
      mfb >>= 1;  /* now there is one less bit available in first byte 每执行一次首位有效字节就少一位 */
    } while (x > mfb);  /* still needs continuation byte? 判断剩下的x能否放入首字节了 */
    // 左移一位是为了生成首字节头的0
    buff[UTF8BUFFSZ - n] = cast(char, (~mfb << 1) | x);  /* add first byte 填充首字节 */
  }
  return n;
}


/* maximum length of the conversion of a number to a string */
// 数字转换成的字符串的最大长度
#define MAXNUMBER2STR	50


/*
** Convert a number object to a string
*/
// 将数字转换成字符串
// 将obj从数字类型转换成字符串类型
// 转换后可能是整数类型也可能是浮点数类型
void vsO_tostring (vs_State *L, StkId obj) {
  char buff[MAXNUMBER2STR];
  size_t len;
  vs_assert(ttisnumber(obj)); // 保证传入的类型是数字类型
  if (ttisinteger(obj))        // 如果是整数类型
    // vs_integer2str(buff, sbuff, i)扩展为snprintf((buff),sbuff,"%" "ll" "d",(long long)(i))
    len = vs_integer2str(buff, sizeof(buff), ivalue(obj));  // 实际调用了snprintf
  else {  // 浮点数类型
    // vs_number2str(buff, sbuff, f)扩展为snprintf((buff),sbuff,"%.14g",(double)(f))
    len = vs_number2str(buff, sizeof(buff), fltvalue(obj));
    // snprintf默认会将可以转换成整数的浮点数转换成整数
    // 例如100.0 会被转换成 "100"
    // strspn 查找str1第一个不在str2中字符的位置
    if (buff[strspn(buff, "-0123456789")] == '\0') {  /* looks like an int? 没有找到小数点 */
      // 在缓存末尾加上".0"
      buff[len++] = vs_getlocaledecpoint();
      buff[len++] = '0';  /* adds '.0' to result */
    }
  }
  // 给TValue设置TString值
  setsvalue2s(L, obj, vsS_newlstr(L, buff, len));
}


// 修改当前栈顶的值为传入的str, l是str的长度
// 并让top向后移动一位
static void pushstr (vs_State *L, const char *str, size_t l) {
  // set string to stack
  setsvalue2s(L, L->top, vsS_newlstr(L, str, l));
  vsD_inctop(L); // 让top后移一位
}


/*
** this function handles only '%d', '%c', '%f', '%p', and '%s'
   conventional formats, plus Lua-specific '%I' and '%U'
*/
// 用于转换fmt生成格式化字符串并放入栈顶, 返回值是格式化后的字符串
// va_arg(ap, type) 在使用type时绝对不能是以下类型, 使用这些类型会进行默认参数提升
// char、signed char、unsigned char
// short、unsigned short
// signed short、short int、signed short int、unsigned short int
// float
const char *vsO_pushvfstring (vs_State *L, const char *fmt, va_list argp) {
  int n = 0;
  for (;;) {
    const char *e = strchr(fmt, '%'); // 找到%开始的位置
    if (e == NULL) break;
    pushstr(L, fmt, e - fmt);  // 压入两个%x之间的字符串
    // e指向%, e+1指向了类型标志 d c f p s I U
    switch (*(e+1)) {
      case 's': {  /* zero-terminated string */
        const char *s = va_arg(argp, char *);
        if (s == NULL) s = "(null)";
        pushstr(L, s, strlen(s));
        break;
      }
      case 'c': {  /* an 'int' as a character */
        char buff = cast(char, va_arg(argp, int)); // 默认参数提升导致char类型用int接收
        if (lisprint(cast_uchar(buff)))
          pushstr(L, &buff, 1);
        else  /* non-printable character; print its code 不是可打印字符打印出来编码 */
          vsO_pushfstring(L, "<\\%d>", cast_uchar(buff));
        break;
      }
      // d I f都需要共同的将top地方的值转换成字符串
      case 'd': {  /* an 'int' */
        setivalue(L->top, va_arg(argp, int));
        goto top2str;
      }
      case 'I': {  /* a 'vs_Integer' */
        setivalue(L->top, cast(vs_Integer, va_arg(argp, l_uacInt)));
        goto top2str;
      }
      case 'f': {  /* a 'vs_Number' */
        setfltvalue(L->top, cast_num(va_arg(argp, l_uacNumber)));
      top2str:  /* convert the top element to a string */
        vsD_inctop(L);
        vsO_tostring(L, L->top - 1);
        break;
      }
      case 'p': {  /* a pointer */
        char buff[4*sizeof(void *) + 8]; /* should be enough space for a '%p' 为打印指针创建足够的空间 */
        int l = l_sprintf(buff, sizeof(buff), "%p", va_arg(argp, void *));
        pushstr(L, buff, l);
        break;
      }
      case 'U': {  /* an 'int' as a UTF-8 sequence 将一个utf8字符构成的字符串压入栈 */
        char buff[UTF8BUFFSZ];
        int l = vsO_utf8esc(buff, cast(long, va_arg(argp, long)));
        pushstr(L, buff + UTF8BUFFSZ - l, l);
        break;
      }
      case '%': {  // 出现%% 代表就是要打印一个%
        pushstr(L, "%", 1);
        break;
      }
      default: {
        vsG_runerror(L, "invalid option '%%%c' to 'vs_pushfstring'",
                         *(e + 1));
      }
    }
    n += 2;     // 每一轮会向栈上增加两个值
    fmt = e+2;  // 跳过当前的%x 继续下一轮
  }
  vsD_checkstack(L, 1);
  pushstr(L, fmt, strlen(fmt));  // 压入最后剩余的字符串
  // 将压入栈的这n+1个值合并起来
  if (n > 0) vsV_concat(L, n + 1);  // 最后又压入了一次所以栈上值的个数又多了一个
  return svalue(L->top - 1);  // 返回所有合并后的结果
}


// 将fmt格式化并将格式化字符串放在栈顶 返回格式化后的字符串
const char *vsO_pushfstring (vs_State *L, const char *fmt, ...) {
  const char *msg;
  va_list argp;
  va_start(argp, fmt);
  msg = vsO_pushvfstring(L, fmt, argp);
  va_end(argp);
  return msg;
}


/* number of chars of a literal string without the ending \0 */
#define LL(x)	(sizeof(x)/sizeof(char) - 1)

#define RETS	"..."
#define PRE	"[string \""
#define POS	"\"]"

#define addstr(a,b,l)	( memcpy(a,b,(l) * sizeof(char)), a += (l) )

// 根据首字符处理source放入out中
// 首字符=  去掉=,直接拷贝剩余部分 超过部分直接截断
// 首字符@  去掉@,直接拷贝剩余部分 放不下拷贝...xxxx, xxxx是source的末尾几位
// 其他情况 处理成格式 [string "source"] 放不下处理成 [string "xxxx..."] xxxx是source的前几位
void vsO_chunkid (char *out, const char *source, size_t bufflen) {
  size_t l = strlen(source);
  if (*source == '=') {  /* 'literal' source */
    if (l <= bufflen)  /* small enough? 向out拷贝source =后面的内容包括\0 */
      memcpy(out, source + 1, l * sizeof(char));
    else {  /* truncate it 填满out 并在最后放上\0 */
      addstr(out, source + 1, bufflen - 1);
      *out = '\0';
    }
  }
  else if (*source == '@') {  /* file name */
    if (l <= bufflen)  /* small enough? */
      memcpy(out, source + 1, l * sizeof(char));
    else {  /* add '...' before rest of name */
      addstr(out, RETS, LL(RETS));  // 拷贝"..."到out中
      bufflen -= LL(RETS);
      // 拷贝source的最后几位
      // 最终结果就是 ...xxxx 前面的部分使用"..."省略, 保留了最后的部分
      memcpy(out, source + 1 + l - bufflen, bufflen * sizeof(char));
    }
  }
  else {  /* string; format as [string "source"] */
    // 将out处理成 [string "source"]
    const char *nl = strchr(source, '\n');  /* find first new line (if any) */
    addstr(out, PRE, LL(PRE));  /* add prefix */
    bufflen -= LL(PRE RETS POS) + 1;  /* save space for prefix+suffix+'\0' 为前缀后缀和\0准备好空间 */
    if (l < bufflen && nl == NULL) {  /* small one-line source? */
      // 没有换行且source能被完全放下 直接全部source拷贝进out
      addstr(out, source, l);  /* keep it */
    }
    else {
      if (nl != NULL) l = nl - source;  /* stop at first newline 如果不止一行那么就只取第一行 */
      if (l > bufflen) l = bufflen;
      addstr(out, source, l);          // 拷贝source的前几位
      addstr(out, RETS, LL(RETS));     // 增加 "..."
    }
    memcpy(out, POS, (LL(POS) + 1) * sizeof(char));  // 将 '"]'以及\0放入out
  }
}

