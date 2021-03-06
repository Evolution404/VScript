#define vs_c



#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vs.h"

#include "vauxlib.h"
#include "vslib.h"



// 表示的是交互式环境的前面的提示符号
#define VS_PROMPT		"> "
#define VS_PROMPT2		">> "

#define VS_PROGNAME		"vs"

/*
** vs_readline defines how to show a prompt and then read a line from
** the standard input.
** vs_saveline defines how to "save" a read line in a "history".
** vs_freeline defines how to free a line read by vs_readline.
*/
#include <readline/readline.h>
#include <readline/history.h>
#define vs_readline(L,b,p)	((void)L, ((b)=readline(p)) != NULL)
#define vs_saveline(L,line)	((void)L, add_history(line))
#define vs_freeline(L,b)	((void)L, free(b))




static vs_State *globalL = NULL;

static const char *progname = VS_PROGNAME;


/*
** Function to be called at a C signal. Because a C signal cannot
** just change a VS state (as there is no proper synchronization),
** this function only sets a hook that, when called, will stop the
** interpreter.
*/
static void laction (int i) {
  signal(i, SIG_DFL); /* if another SIGINT happens, terminate process */
}


/*
** Prints an error message, adding the program name in front of it
** (if present)
*/
static void l_message (const char *pname, const char *msg) {
  if (pname) vs_writestringerror("%s: ", pname);
  vs_writestringerror("%s\n", msg);
}


/*
** Check whether 'status' is not OK and, if so, prints the error
** message on the top of the stack. It assumes that the error object
** is a string, as it was either generated by VS or by 'msghandler'.
*/
// 打印存放在栈顶的错误信息,打印后将栈顶元素弹出
static int report (vs_State *L, int status) {
  if (status != VS_OK) {
    const char *msg = vs_tostring(L, -1);
    l_message(progname, msg);
    vs_pop(L, 1);  /* remove message */
  }
  return status;
}


/*
** Message handler used to run all chunks
*/
static int msghandler (vs_State *L) {
  const char *msg = vs_tostring(L, 1);
  if (msg == NULL) {  /* is error object not a string? */
    msg = vs_pushfstring(L, "(error object is a %s value)",
                             vsL_typename(L, 1));
  }
  vsL_traceback(L, L, msg, 1);  /* append a standard traceback */
  return 1;  /* return the traceback */
}


/*
** Interface to 'vs_pcall', which sets appropriate message function
** and C-signal handler. Used to run all chunks.
*/
static int docall (vs_State *L, int narg, int nres) {
  int status;
  // base位置放着错误处理函数
  int base = vs_gettop(L) - narg;  /* function index */
  vs_pushcfunction(L, msghandler);  /* push message handler */
  // 将错误处理函数放到被调用的函数前面
  vs_insert(L, base);  /* put it under function and args */
  globalL = L;  /* to be available to 'laction' */
  signal(SIGINT, laction);  /* set C-signal handler */
  status = vs_pcall(L, narg, nres, base);
  signal(SIGINT, SIG_DFL); /* reset C-signal handler */
  vs_remove(L, base);  /* remove message handler from the stack */
  return status;
}


// 打印VS_COPYRIGHT并加上一个\n
static void print_version (void) {
  vs_writestring(VS_COPYRIGHT, strlen(VS_COPYRIGHT));
  vs_writeline();
}


/*
** Create the 'arg' table, which stores all arguments from the
** command line ('argv'). It should be aligned so that, at index 0,
** it has 'argv[script]', which is the script name. The arguments
** to the script (everything after 'script') go to positive indices;
** other arguments (before the script name) go to negative indices.
** If there is no script name, assume interpreter's name as base.
*/
// 在执行vs命令时例如 ./vs my.vs a b c
// 文件名my.vs后面的就代表传递的参数,arg[1]就等于a,arg[2]=b,arg[3]=c
// 而arg[0]=my.vs,保存了文件名,再往前的参数就保存为负数
static void createargtable (vs_State *L, char **argv, int argc, int script) {
  int i, narg;
  if (script == argc) script = 0;  /* no script name? */
  narg = argc - (script + 1);  /* number of positive indices */
  vs_createtable(L, narg, script + 1);
  for (i = 0; i < argc; i++) {
    vs_pushstring(L, argv[i]);
    vs_seti(L, -2, i - script);
  }
  // 设置arg参数
  vs_setglobal(L, "arg");
}


/*
** Returns the string to be used as a prompt by the interpreter.
*/
// 根据是不是第一行返回不同的提示符号
static const char *get_prompt (vs_State *L, int firstline) {
  const char *p;
  vs_getglobal(L, firstline ? "_PROMPT" : "_PROMPT2");
  p = vs_tostring(L, -1);
  if (p == NULL) p = (firstline ? VS_PROMPT : VS_PROMPT2);
  return p;
}

/* mark in error messages for incomplete statements */
#define EOFMARK		"<eof>"
#define marklen		(sizeof(EOFMARK)/sizeof(char) - 1)


/*
** Check whether 'status' signals a syntax error and the error
** message at the top of the stack ends with the above mark for
** incomplete statements.
*/
// 如果错误信息结尾是"<eof>"就认为这个语句还没结束
// 删除栈内错误信息,并返回1
static int incomplete (vs_State *L, int status) {
  if (status == VS_ERRSYNTAX) {
    size_t lmsg;
    const char *msg = vs_tolstring(L, -1, &lmsg);
    if (lmsg >= marklen && strcmp(msg + lmsg - marklen, EOFMARK) == 0) {
      vs_pop(L, 1);
      return 1;
    }
  }
  return 0;  /* else... */
}


/*
** Prompt the user, read a line, and push it into the VS stack.
*/
// 通过调用readline读入一行字符串压入栈顶,返回0说明没读取到字符串
static int pushline (vs_State *L, int firstline) {
  char *b;
  size_t l;
  const char *prmt = get_prompt(L, firstline);
  // 读取一行内容到b中,如果按了ctrl-d,readstatus为0
  int readstatus = vs_readline(L, b, prmt);
  // 没读到内容直接返回0,栈中的prompt会在doREPL中一次性清除
  if (readstatus == 0)
    return 0;  /* no input (prompt will be popped by caller) */
  // 弹出提示符号
  vs_pop(L, 1);  /* remove prompt */
  l = strlen(b);
  if (l > 0 && b[l-1] == '\n')  /* line ends with newline? */
    b[--l] = '\0';  /* remove it */
  if (firstline && b[0] == '=')  /* for compatibility with 5.2, ... */
    vs_pushfstring(L, "return %s", b + 1);  /* change '=' to 'return' */
  else
    vs_pushlstring(L, b, l);
  // 如果使用的readline库,读入的字符串使用后需要被释放
  vs_freeline(L, b);
  return 1;
}


/*
** Try to compile line on the stack as 'return <line>;'; on return, stack
** has either compiled chunk or original line (if compilation failed).
*/
static int addreturn (vs_State *L) {
  // 读取用户输入的那一行
  const char *line = vs_tostring(L, -1);  /* original line */
  // 在用户输入的行前面加上return
  const char *retline = vs_pushfstring(L, "return %s;", line);
  int status = vsL_loadbuffer(L, retline, strlen(retline), "=stdin");
  // 执行成功后删除加上return的哪一行,并且保存用户输入
  if (status == VS_OK) {
    vs_remove(L, -2);  /* remove modified line */
    if (line[0] != '\0')  /* non empty? */
      vs_saveline(L, line);  /* keep history */
  }
  else
    vs_pop(L, 2);  /* pop result from 'vsL_loadbuffer' and modified line */
  return status;
}


/*
** Read multiple lines until a complete VS statement
*/
static int multiline (vs_State *L) {
  for (;;) {  /* repeat until gets a complete statement */
    size_t len;
    const char *line = vs_tolstring(L, 1, &len);  /* get what it has */
    int status = vsL_loadbuffer(L, line, len, "=stdin");  /* try it */
    if (!incomplete(L, status) || !pushline(L, 0)) {
      vs_saveline(L, line);  /* keep history */
      return status;  /* cannot or should not try to add continuation line */
    }
    // 压入一个回车
    vs_pushliteral(L, "\n");  /* add newline... */
    // 原来是 旧行 新行 回车
    // 执行insert后变成 旧行 回车 新行
    vs_insert(L, -2);  /* ...between the two lines */
    // 将 旧行 回车 新行 三个字符串连接成一个
    vs_concat(L, 3);  /* join them */
  }
}


/*
** Read a line and try to load (compile) it first as an expression (by
** adding "return " in front of it) and second as a statement. Return
** the final status of load/call with the resulting function (if any)
** in the top of the stack.
*/
// 读取用户输入的一行,并执行这一行
static int loadline (vs_State *L) {
  int status;
  vs_settop(L, 0);
  if (!pushline(L, 1))
    return -1;  /* no input */
  if ((status = addreturn(L)) != VS_OK)  /* 'return ...' did not work? */
    status = multiline(L);  /* try as command, maybe with continuation lines */
  // 删除用户输入的那一行
  vs_remove(L, 1);  /* remove line from the stack */
  vs_assert(vs_gettop(L) == 1);
  return status;
}


/*
** Prints (calling the VS 'print' function) any values on the stack
*/
static void l_print (vs_State *L) {
  int n = vs_gettop(L);
  if (n > 0) {  /* any result to be printed? */
    vsL_checkstack(L, VS_MINSTACK, "too many results to print");
    // 取出print函数
    vs_getglobal(L, "print");
    // 将print函数放到1位置
    vs_insert(L, 1);
    // 调用1位置的print函数,打印n个参数
    if (vs_pcall(L, n, 0, 0) != VS_OK)
      l_message(progname, vs_pushfstring(L, "error calling 'print' (%s)",
                                             vs_tostring(L, -1)));
  }
}


/*
** Do the REPL: repeatedly read (load) a line, evavste (call) it, and
** print any results.
*/
static void doREPL (vs_State *L) {
  int status;
  const char *oldprogname = progname;
  progname = NULL;  /* no 'progname' on errors in interactive mode */
  while ((status = loadline(L)) != -1) {
    // 执行用户的输入
    if (status == VS_OK)
      status = docall(L, 0, VS_MULTRET);
    // 打印所有返回值
    if (status == VS_OK) l_print(L);
    else report(L, status);
  }
  vs_settop(L, 0);  /* clear stack */
  vs_writeline();
  progname = oldprogname;
}


/*
** Push on the stack the contents of table 'arg' from 1 to #arg
*/
// 将arg表中的正数元素挨个放入栈顶,返回放入的元素个数
static int pushargs (vs_State *L) {
  int i, n;
  if (vs_getglobal(L, "arg") != VS_TTABLE)
    vsL_error(L, "'arg' is not a table");
  n = (int)vsL_len(L, -1);
  vsL_checkstack(L, n + 3, "too many arguments to script");
  for (i = 1; i <= n; i++)
    // -i位置保存着arg表,在arg表中查询key是i的项保存到栈顶
    vs_geti(L, -i, i);
  // 删除arg表
  vs_remove(L, -i);  /* remove table from the stack */
  return n;
}


// 通过命令行参数加载被执行的文件
static int handle_script (vs_State *L, char **argv) {
  int status;
  const char *fname = argv[0];
  if (strcmp(fname, "-") == 0 && strcmp(argv[-1], "--") != 0)
    fname = NULL;  /* stdin */
  // fname=NULL代表从stdin输入
  // 执行后栈顶保存着vs闭包
  status = vsL_loadfile(L, fname);
  if (status == VS_OK) {
    int n = pushargs(L);  /* push arguments to script */
    status = docall(L, n, VS_MULTRET);
  }
  return report(L, status);
}


/*
** Main body of stand-alone interpreter (to be called in protected mode).
** Reads the options and handles them all.
*/
// pmain方法是整个VS执行流最核心的方法
// 主要负责:命令行参数的解析,VS语言默认库的加载,VS脚本语言的解析和调用等
// 栈上有两个参数,就是main函数的argc和argv
static int pmain (vs_State *L) {
  // 取出argc和argv
  int argc = (int)vs_tointeger(L, 1);
  char **argv = (char **)vs_touserdata(L, 2);
  if (argv[0] && argv[0][0]) progname = argv[0];
  // 加载所有的库
  vsL_openlibs(L);  /* open standard libraries */
  createargtable(L, argv, argc, 1);  /* create table 'arg' */
  if (argc > 1 && handle_script(L, argv + 1) != VS_OK)
    return 0;
  if (argc == 1) {  /* no arguments? */
    print_version();
    doREPL(L);  /* do read-eval-print loop */
  }
  vs_pushboolean(L, 1);  /* signal no errors */
  return 1;
}


int main (int argc, char **argv) {
  int status, result;
  // 创建vs的全局状态机
  vs_State *L = vsL_newstate();  /* create state */
  if (L == NULL) {
    l_message(argv[0], "cannot create state: not enough memory");
    return EXIT_FAILURE;
  }
  // push系列是往栈中压入元素
  vs_pushcfunction(L, &pmain);  /* to call 'pmain' in protected mode */
  vs_pushinteger(L, argc);  /* 1st argument */
  vs_pushlightuserdata(L, argv); /* 2nd argument */
  // 调用pmain函数,传入两个参数,有一个返回值
  status = vs_pcall(L, 2, 1, 0);  /* do the call */
  result = vs_toboolean(L, -1);  /* get result */
  report(L, status);
  vs_close(L);
  return (result && status == VS_OK) ? EXIT_SUCCESS : EXIT_FAILURE;
}

