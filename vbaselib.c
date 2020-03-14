#define vbaselib_c

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vs.h"

#include "vauxlib.h"
#include "vslib.h"


// print函数的实现
static int vsB_print (vs_State *L) {
  int n = vs_gettop(L);  /* number of arguments top-func-1 栈上参数的个数 */
  int i;
  vs_getglobal(L, "tostring");  // _G.tostring放到栈顶
  for (i=1; i<=n; i++) {
    const char *s;
    size_t l;
    vs_pushvalue(L, -1);  /* function to be called 拷贝一份tostring */
    vs_pushvalue(L, i);   /* value to print 压入要打印的值 */
    vs_call(L, 1, 1);  // 调用tostring 转换第一个参数
    s = vs_tolstring(L, -1, &l);  /* get result 处理在栈顶的tostring函数的返回值 */
    if (s == NULL)
      return vsL_error(L, "'tostring' must return a string to 'print'");
    if (i>1) vs_writestring("\t", 1);  // print多个变量中间使用\t分隔
    vs_writestring(s, l);  // 打印此次处理的变量
    vs_pop(L, 1);  /* pop result 弹出栈顶的tostring的返回值 */
  }
  vs_writeline();
  return 0;
}


#define SPACECHARS	" \f\n\r\t\v"

static const char *b_str2int (const char *s, int base, vs_Integer *pn) {
  vs_Unsigned n = 0;
  int neg = 0;
  s += strspn(s, SPACECHARS);  /* skip initial spaces */
  if (*s == '-') { s++; neg = 1; }  /* handle signal */
  else if (*s == '+') s++;
  if (!isalnum((unsigned char)*s))  /* no digit? */
    return NULL;
  do {
    int digit = (isdigit((unsigned char)*s)) ? *s - '0'
                   : (toupper((unsigned char)*s) - 'A') + 10;
    if (digit >= base) return NULL;  /* invalid numeral */
    n = n * base + digit;
    s++;
  } while (isalnum((unsigned char)*s));
  s += strspn(s, SPACECHARS);  /* skip trailing spaces */
  *pn = (vs_Integer)((neg) ? (0u - n) : n);
  return s;
}


static int vsB_tonumber (vs_State *L) {
  if (vs_isnoneornil(L, 2)) {  /* standard conversion? */
    vsL_checkany(L, 1);
    if (vs_type(L, 1) == VS_TNUMBER) {  /* already a number? */
      vs_settop(L, 1);  /* yes; return it */
      return 1;
    }
    else {
      size_t l;
      const char *s = vs_tolstring(L, 1, &l);
      if (s != NULL && vs_stringtonumber(L, s) == l + 1)
        return 1;  /* successful conversion to number */
      /* else not a number */
    }
  }
  else {
    size_t l;
    const char *s;
    vs_Integer n = 0;  /* to avoid warnings */
    vs_Integer base = vsL_checkinteger(L, 2);
    vsL_checktype(L, 1, VS_TSTRING);  /* no numbers as strings */
    s = vs_tolstring(L, 1, &l);
    vsL_argcheck(L, 2 <= base && base <= 36, 2, "base out of range");
    if (b_str2int(s, (int)base, &n) == s + l) {
      vs_pushinteger(L, n);
      return 1;
    }  /* else not a number */
  }  /* else not a number */
  vs_pushnil(L);  /* not a number */
  return 1;
}


static int vsB_error (vs_State *L) {
  int level = (int)vsL_optinteger(L, 2, 1);
  vs_settop(L, 1);
  if (vs_type(L, 1) == VS_TSTRING && level > 0) {
    vsL_where(L, level);   /* add extra information */
    vs_pushvalue(L, 1);
    vs_concat(L, 2);
  }
  return vs_error(L);
}


static int vsB_equal (vs_State *L) {
  vsL_checkany(L, 1);
  vsL_checkany(L, 2);
  vs_pushboolean(L, vs_equal(L, 1, 2));
  return 1;
}


static int vsB_len (vs_State *L) {
  int t = vs_type(L, 1);
  vsL_argcheck(L, t == VS_TTABLE || t == VS_TSTRING, 1,
                   "table or string expected");
  vs_pushinteger(L, vs_rawlen(L, 1));
  return 1;
}


static int vsB_get (vs_State *L) {
  vsL_checktype(L, 1, VS_TTABLE);
  vsL_checkany(L, 2);
  vs_settop(L, 2);
  vs_gettable(L, 1);
  return 1;
}

static int vsB_set (vs_State *L) {
  vsL_checktype(L, 1, VS_TTABLE);
  vsL_checkany(L, 2);
  vsL_checkany(L, 3);
  vs_settop(L, 3);
  vs_settable(L, 1);
  return 1;
}

static int vsB_type (vs_State *L) {
  int t = vs_type(L, 1);
  vsL_argcheck(L, t != VS_TNONE, 1, "value expected");
  vs_pushstring(L, vs_typename(L, t));
  return 1;
}


static int pairsmeta (vs_State *L, int iszero, vs_CFunction iter) {
  vsL_checkany(L, 1);
  vs_pushcfunction(L, iter);  /* will return generator, */
  vs_pushvalue(L, 1);  /* state, */
  if (iszero) vs_pushinteger(L, 0);  /* and initial value */
  else vs_pushnil(L);
  return 3;
}


static int vsB_next (vs_State *L) {
  vsL_checktype(L, 1, VS_TTABLE);
  vs_settop(L, 2);  /* create a 2nd argument if there isn't one */
  if (vs_next(L, 1))
    return 2;
  else {
    vs_pushnil(L);
    return 1;
  }
}


static int vsB_pairs (vs_State *L) {
  return pairsmeta(L, 0, vsB_next);
}


/*
** Traversal function for 'ipairs'
*/
static int ipairsaux (vs_State *L) {
  vs_Integer i = vsL_checkinteger(L, 2) + 1;
  vs_pushinteger(L, i);
  return (vs_geti(L, 1, i) == VS_TNIL) ? 1 : 2;
}


/*
** 'ipairs' function. Returns 'ipairsaux', given "table", 0.
** (The given "table" may not be a table.)
*/
static int vsB_ipairs (vs_State *L) {
  vsL_checkany(L, 1);
  vs_pushcfunction(L, ipairsaux);  /* iteration function */
  vs_pushvalue(L, 1);  /* state */
  vs_pushinteger(L, 0);  /* initial value */
  return 3;
}


static int load_aux (vs_State *L, int status, int envidx) {
  if (status == VS_OK) {
    if (envidx != 0) {  /* 'env' parameter? */
      vs_pushvalue(L, envidx);  /* environment for loaded function */
      if (!vs_setupvalue(L, -2, 1))  /* set it as 1st upvalue */
        vs_pop(L, 1);  /* remove 'env' if not used by previous call */
    }
    return 1;
  }
  else {  /* error (message is on top of the stack) */
    vs_pushnil(L);
    vs_insert(L, -2);  /* put before error message */
    return 2;  /* return nil plus error message */
  }
}


static int vsB_loadfile (vs_State *L) {
  const char *fname = vsL_optstring(L, 1, NULL);
  const char *mode = vsL_optstring(L, 2, NULL);
  int env = (!vs_isnone(L, 3) ? 3 : 0);  /* 'env' index or 0 if no 'env' */
  int status = vsL_loadfilex(L, fname, mode);
  return load_aux(L, status, env);
}


/*
** {======================================================
** Generic Read function
** =======================================================
*/


/*
** reserved slot, above all arguments, to hold a copy of the returned
** string to avoid it being collected while parsed. 'load' has four
** optional arguments (chunk, source name, mode, and environment).
*/
#define RESERVEDSLOT	5


/*
** Reader for generic 'load' function: 'vs_load' uses the
** stack for internal stuff, so the reader cannot change the
** stack top. Instead, it keeps its resulting string in a
** reserved slot inside the stack.
*/
static const char *generic_reader (vs_State *L, void *ud, size_t *size) {
  (void)(ud);  /* not used */
  vsL_checkstack(L, 2, "too many nested functions");
  vs_pushvalue(L, 1);  /* get function */
  vs_call(L, 0, 1);  /* call it */
  if (vs_isnil(L, -1)) {
    vs_pop(L, 1);  /* pop result */
    *size = 0;
    return NULL;
  }
  else if (!vs_isstring(L, -1))
    vsL_error(L, "reader function must return a string");
  vs_replace(L, RESERVEDSLOT);  /* save string in reserved slot */
  return vs_tolstring(L, RESERVEDSLOT, size);
}


static int vsB_load (vs_State *L) {
  int status;
  size_t l;
  const char *s = vs_tolstring(L, 1, &l);
  const char *mode = vsL_optstring(L, 3, "bt");
  int env = (!vs_isnone(L, 4) ? 4 : 0);  /* 'env' index or 0 if no 'env' */
  if (s != NULL) {  /* loading a string? */
    const char *chunkname = vsL_optstring(L, 2, s);
    status = vsL_loadbufferx(L, s, l, chunkname, mode);
  }
  else {  /* loading from a reader function */
    const char *chunkname = vsL_optstring(L, 2, "=(load)");
    vsL_checktype(L, 1, VS_TFUNCTION);
    vs_settop(L, RESERVEDSLOT);  /* create reserved slot */
    status = vs_load(L, generic_reader, NULL, chunkname, mode);
  }
  return load_aux(L, status, env);
}

/* }====================================================== */


static int dofilecont (vs_State *L) {
  return vs_gettop(L) - 1;
}


static int vsB_dofile (vs_State *L) {
  const char *fname = vsL_optstring(L, 1, NULL);
  vs_settop(L, 1);
  if (vsL_loadfile(L, fname) != VS_OK)
    return vs_error(L);
  vs_call(L, 0, VS_MULTRET);
  return dofilecont(L);
}


static int vsB_assert (vs_State *L) {
  if (vs_toboolean(L, 1))  /* condition is true? */
    return vs_gettop(L);  /* return all arguments */
  else {  /* error */
    vsL_checkany(L, 1);  /* there must be a condition */
    vs_remove(L, 1);  /* remove it */
    vs_pushliteral(L, "assertion failed!");  /* default message */
    vs_settop(L, 1);  /* leave only message (default if no other one) */
    return vsB_error(L);  /* call 'error' */
  }
}


static int vsB_select (vs_State *L) {
  int n = vs_gettop(L);
  if (vs_type(L, 1) == VS_TSTRING && *vs_tostring(L, 1) == '#') {
    vs_pushinteger(L, n-1);
    return 1;
  }
  else {
    vs_Integer i = vsL_checkinteger(L, 1);
    if (i < 0) i = n + i;
    else if (i > n) i = n;
    vsL_argcheck(L, 1 <= i, 1, "index out of range");
    return n - (int)i;
  }
}


/*
** Continuation function for 'pcall' and 'xpcall'. Both functions
** already pushed a 'true' before doing the call, so in case of success
** 'finishpcall' only has to return everything in the stack minus
** 'extra' values (where 'extra' is exactly the number of items to be
** ignored).
*/
static int finishpcall (vs_State *L, int status) {
  if (status != VS_OK) {  /* error? */
    vs_pushboolean(L, 0);  /* first result (false) */
    vs_pushvalue(L, -2);  /* error message */
    return 2;  /* return false, msg */
  }
  else
    return vs_gettop(L);  /* return all results */
}


static int vsB_pcall (vs_State *L) {
  int status;
  vsL_checkany(L, 1);
  vs_pushboolean(L, 1);  /* first result if no errors */
  vs_insert(L, 1);  /* put it in place */
  status = vs_pcall(L, vs_gettop(L) - 2, VS_MULTRET, 0);
  return finishpcall(L, status);
}


/*
** Do a protected call with error handling. After 'vs_rotate', the
** stack will have <f, err, true, f, [args...]>; so, the function passes
** 2 to 'finishpcall' to skip the 2 first values when returning results.
*/
static int vsB_xpcall (vs_State *L) {
  int status;
  int n = vs_gettop(L);
  vsL_checktype(L, 2, VS_TFUNCTION);  /* check error function */
  vs_pushboolean(L, 1);  /* first result */
  vs_pushvalue(L, 1);  /* function */
  vs_rotate(L, 3, 2);  /* move them below function's arguments */
  status = vs_pcall(L, n - 2, VS_MULTRET, 2);
  return finishpcall(L, status);
}


static int vsB_tostring (vs_State *L) {
  vsL_checkany(L, 1);
  vsL_tolstring(L, 1, NULL);
  return 1;
}


static const vsL_Reg base_funcs[] = {
  {"assert", vsB_assert},
  {"dofile", vsB_dofile},
  {"error", vsB_error},
  {"ipairs", vsB_ipairs},
  {"loadfile", vsB_loadfile},
  {"load", vsB_load},
  {"next", vsB_next},
  {"pairs", vsB_pairs},
  {"pcall", vsB_pcall},
  {"print", vsB_print},
  {"equal", vsB_equal},
  {"len", vsB_len},
  {"get", vsB_get},
  {"set", vsB_set},
  {"select", vsB_select},
  {"tonumber", vsB_tonumber},
  {"tostring", vsB_tostring},
  {"type", vsB_type},
  {"xpcall", vsB_xpcall},
  /* placeholders */
  {"_G", NULL},
  {"_VERSION", NULL},
  {NULL, NULL}
};


// 加载base模块
// 初始化_G._G和_G._VERSION
VSMOD_API int vsopen_base (vs_State *L) {
  /* open lib into global table */
  vs_pushglobaltable(L);  // 将_G表压入栈顶
  // 设置_G.assert _G.collectgarbage ....就是 base_funcs数组的所有值
  vsL_setfuncs(L, base_funcs, 0);
  /* set global _G */
  vs_pushvalue(L, -1);
  vs_setfield(L, -2, "_G");  // _G._G = _G
  /* set global _VERSION */
  vs_pushliteral(L, VS_VERSION);  // 栈顶压入 "VS 5.3"
  vs_setfield(L, -2, "_VERSION");  // _G._VERSION = "VS 5.3"
  return 1;
}

