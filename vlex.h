#ifndef vlex_h
#define vlex_h

#include "vobject.h"
#include "vzio.h"


// 大于256可能是为了避开ASCII的范围 vsX_token2str
#define FIRST_RESERVED	257

#define VS_ENV		"_ENV"


/*
* WARNING: if you change the order of this enumeration,
* grep "ORDER RESERVED"
*/
// 终结符的枚举值 前面一部分是保留字 后面部分是其余终结符
enum RESERVED {
  /* terminal symbols denoted by reserved words 以保留字形式表示的终结符 */
  // 这些枚举值从257开始 避免前256占用了ascii字符
  TK_AND = FIRST_RESERVED, TK_BREAK,
  TK_DO, TK_ELSE, TK_ELSEIF, TK_FALSE, TK_FOR, TK_FUNCTION,
  TK_GOTO, TK_IF, TK_IN, TK_LOCAL, TK_NIL, TK_NOT, TK_OR, TK_REPEAT,
  TK_RETURN, TK_THEN, TK_TRUE, TK_UNTIL, TK_WHILE,
  /* other terminal symbols 其他终结符 */
  TK_IDIV, TK_CONCAT, TK_DOTS, TK_EQ, TK_GE, TK_LE, TK_NE,
  TK_SHL, TK_SHR,
  TK_DBCOLON, TK_EOS,
  TK_FLT, TK_INT, TK_NAME, TK_STRING
};

/* number of reserved words */
// 保留字的个数 TK_AND到TK_WHILE的个数
#define NUM_RESERVED	(cast(int, TK_WHILE-FIRST_RESERVED+1))  // 22


// 浮点数 整数和字符串的共用体构成的语义信息
typedef union {
  vs_Number r;
  vs_Integer i;
  TString *ts;
} SemInfo;  /* semantics information 语义信息 */


// 一个符号信息
typedef struct Token {
  int token;
  SemInfo seminfo;
} Token;


/* state of the lexer plus state of the parser when shared by all
   functions */
// 词法状态 叫做词法状态不太贴切,因为它其实保存了整个编译系统的状态,不只是词法分析
typedef struct LexState {
  int current;  /* current character (charint) 当前分析的字符(用int表示char) */
  int linenumber;  /* input line counter 输入的行号 */
  int lastline;  /* line of last token 'consumed' 最后一个被消耗的符号的行号 */
  Token t;  /* current token 当前的符号信息 */
  Token lookahead;  /* look ahead token 前一个符号信息 */
  // 当前语法分析器正在分析的函数
  struct FuncState *fs;  /* current function (parser) 当前函数(针对parser) */
  struct vs_State *L;
  ZIO *z;  /* input stream 输入流 */
  Mbuffer *buff;  /* buffer for tokens 符号缓存 */
  // 该表key存放了常量 value代表常量在Proto对象常量表的下标
  // vsX_newstring也使用了这个表存放已经生成过的字符串,保证相同的字符串一定有相同的地址
  Table *h;  /* to avoid collection/reuse strings */
  struct Dyndata *dyd;  /* dynamic structures used by the parser 被parser使用的动态结构 */
  TString *source;  /* current source name 当前源码文件名 */
  TString *envn;  /* environment variable name 环境变量的名称 */
} LexState;


VSI_FUNC void vsX_init (vs_State *L);
VSI_FUNC void vsX_setinput (vs_State *L, LexState *ls, ZIO *z,
                              TString *source, int firstchar);
VSI_FUNC TString *vsX_newstring (LexState *ls, const char *str, size_t l);
VSI_FUNC void vsX_next (LexState *ls);
VSI_FUNC int vsX_lookahead (LexState *ls);
VSI_FUNC l_noret vsX_syntaxerror (LexState *ls, const char *s);
VSI_FUNC const char *vsX_token2str (LexState *ls, int token);


#endif
