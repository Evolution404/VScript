#define vauxlib_c

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vs.h"

#include "vauxlib.h"

/*
** {======================================================
** Traceback
** =======================================================
*/


#define LEVELS1	10	/* size of the first part of the stack */
#define LEVELS2	11	/* size of the second part of the stack */



/*
** search for 'objidx' in table at index -1.
** return 1 + string at top if find a good name.
*/
static int findfield (vs_State *L, int objidx, int level) {
  if (level == 0 || !vs_istable(L, -1))
    return 0;  /* not found */
  vs_pushnil(L);  /* start 'next' loop */
  while (vs_next(L, -2)) {  /* for each pair in table */
    if (vs_type(L, -2) == VS_TSTRING) {  /* ignore non-string keys */
      if (vs_equal(L, objidx, -1)) {  /* found object? */
        vs_pop(L, 1);  /* remove value (but keep name) */
        return 1;
      }
      else if (findfield(L, objidx, level - 1)) {  /* try recursively */
        vs_remove(L, -2);  /* remove table (but keep name) */
        vs_pushliteral(L, ".");
        vs_insert(L, -2);  /* place '.' between the two names */
        vs_concat(L, 3);
        return 1;
      }
    }
    vs_pop(L, 1);  /* remove value */
  }
  return 0;  /* not found */
}


/*
** Search for a name for a function in all loaded modules
*/
static int pushglobalfuncname (vs_State *L, vs_Debug *ar) {
  int top = vs_gettop(L);
  vs_getinfo(L, "f", ar);  /* push function */
  vs_getfield(L, VS_REGISTRYINDEX, VS_LOADED_TABLE);
  if (findfield(L, top + 1, 2)) {
    const char *name = vs_tostring(L, -1);
    if (strncmp(name, "_G.", 3) == 0) {  /* name start with '_G.'? */
      vs_pushstring(L, name + 3);  /* push name without prefix */
      vs_remove(L, -2);  /* remove original name */
    }
    vs_copy(L, -1, top + 1);  /* move name to proper place */
    vs_pop(L, 2);  /* remove pushed values */
    return 1;
  }
  else {
    vs_settop(L, top);  /* remove function and global table */
    return 0;
  }
}


static void pushfuncname (vs_State *L, vs_Debug *ar) {
  if (pushglobalfuncname(L, ar)) {  /* try first a global name */
    vs_pushfstring(L, "function '%s'", vs_tostring(L, -1));
    vs_remove(L, -2);  /* remove name */
  }
  else if (*ar->namewhat != '\0')  /* is there a name from code? */
    vs_pushfstring(L, "%s '%s'", ar->namewhat, ar->name);  /* use it */
  else if (*ar->what == 'm')  /* main? */
      vs_pushliteral(L, "main chunk");
  else if (*ar->what != 'C')  /* for VS functions, use <file:line> */
    vs_pushfstring(L, "function <%s:%d>", ar->short_src, ar->linedefined);
  else  /* nothing left... */
    vs_pushliteral(L, "?");
}


static int lastlevel (vs_State *L) {
  vs_Debug ar;
  int li = 1, le = 1;
  /* find an upper bound */
  while (vs_getstack(L, le, &ar)) { li = le; le *= 2; }
  /* do a binary search */
  while (li < le) {
    int m = (li + le)/2;
    if (vs_getstack(L, m, &ar)) li = m + 1;
    else le = m;
  }
  return le - 1;
}


VSLIB_API void vsL_traceback (vs_State *L, vs_State *L1,
                                const char *msg, int level) {
  vs_Debug ar;
  int top = vs_gettop(L);
  int last = lastlevel(L1);
  int n1 = (last - level > LEVELS1 + LEVELS2) ? LEVELS1 : -1;
  if (msg)
    vs_pushfstring(L, "%s\n", msg);
  vsL_checkstack(L, 10, NULL);
  vs_pushliteral(L, "stack traceback:");
  while (vs_getstack(L1, level++, &ar)) {
    if (n1-- == 0) {  /* too many levels? */
      vs_pushliteral(L, "\n\t...");  /* add a '...' */
      level = last - LEVELS2 + 1;  /* and skip to last ones */
    }
    else {
      vs_getinfo(L1, "Slnt", &ar);
      vs_pushfstring(L, "\n\t%s:", ar.short_src);
      if (ar.currentline > 0)
        vs_pushfstring(L, "%d:", ar.currentline);
      vs_pushliteral(L, " in ");
      pushfuncname(L, &ar);
      vs_concat(L, vs_gettop(L) - top);
    }
  }
  vs_concat(L, vs_gettop(L) - top);
}

/* }====================================================== */


/*
** {======================================================
** Error-report functions
** =======================================================
*/

VSLIB_API int vsL_argerror (vs_State *L, int arg, const char *extramsg) {
  vs_Debug ar;
  if (!vs_getstack(L, 0, &ar))  /* no stack frame? */
    return vsL_error(L, "bad argument #%d (%s)", arg, extramsg);
  vs_getinfo(L, "n", &ar);
  if (strcmp(ar.namewhat, "method") == 0) {
    arg--;  /* do not count 'self' */
    if (arg == 0)  /* error is in the self argument itself? */
      return vsL_error(L, "calling '%s' on bad self (%s)",
                           ar.name, extramsg);
  }
  if (ar.name == NULL)
    ar.name = (pushglobalfuncname(L, &ar)) ? vs_tostring(L, -1) : "?";
  return vsL_error(L, "bad argument #%d to '%s' (%s)",
                        arg, ar.name, extramsg);
}


static int typeerror (vs_State *L, int arg, const char *tname) {
  const char *msg;
  const char *typearg;  /* name for the type of the actual argument */
  if (vs_type(L, arg) == VS_TLIGHTUSERDATA)
    typearg = "light userdata";  /* special name for messages */
  else
    typearg = vsL_typename(L, arg);  /* standard name */
  msg = vs_pushfstring(L, "%s expected, got %s", tname, typearg);
  return vsL_argerror(L, arg, msg);
}


static void tag_error (vs_State *L, int arg, int tag) {
  typeerror(L, arg, vs_typename(L, tag));
}


/*
** The use of 'vs_pushfstring' ensures this function does not
** need reserved stack space when called.
*/
VSLIB_API void vsL_where (vs_State *L, int level) {
  vs_Debug ar;
  if (vs_getstack(L, level, &ar)) {  /* check function at level */
    vs_getinfo(L, "Sl", &ar);  /* get info about it */
    if (ar.currentline > 0) {  /* is there info? */
      vs_pushfstring(L, "%s:%d: ", ar.short_src, ar.currentline);
      return;
    }
  }
  vs_pushfstring(L, "");  /* else, no information available... */
}


/*
** Again, the use of 'vs_pushvfstring' ensures this function does
** not need reserved stack space when called. (At worst, it generates
** an error with "stack overflow" instead of the given message.)
*/
VSLIB_API int vsL_error (vs_State *L, const char *fmt, ...) {
  va_list argp;
  va_start(argp, fmt);
  vsL_where(L, 1);
  vs_pushvfstring(L, fmt, argp);
  va_end(argp);
  vs_concat(L, 2);
  return vs_error(L);
}


VSLIB_API int vsL_fileresult (vs_State *L, int stat, const char *fname) {
  int en = errno;  /* calls to VS API may change this value */
  if (stat) {
    vs_pushboolean(L, 1);
    return 1;
  }
  else {
    vs_pushnil(L);
    if (fname)
      vs_pushfstring(L, "%s: %s", fname, strerror(en));
    else
      vs_pushstring(L, strerror(en));
    vs_pushinteger(L, en);
    return 3;
  }
}


#if !defined(l_inspectstat)	/* { */

#if defined(VS_USE_POSIX)

#include <sys/wait.h>

/*
** use appropriate macros to interpret 'pclose' return status
*/
#define l_inspectstat(stat,what)  \
   if (WIFEXITED(stat)) { stat = WEXITSTATUS(stat); } \
   else if (WIFSIGNALED(stat)) { stat = WTERMSIG(stat); what = "signal"; }

#else

#define l_inspectstat(stat,what)  /* no op */

#endif

#endif				/* } */


VSLIB_API int vsL_execresult (vs_State *L, int stat) {
  const char *what = "exit";  /* type of termination */
  if (stat == -1)  /* error? */
    return vsL_fileresult(L, 0, NULL);
  else {
    l_inspectstat(stat, what);  /* interpret result */
    if (*what == 'e' && stat == 0)  /* successful termination? */
      vs_pushboolean(L, 1);
    else
      vs_pushnil(L);
    vs_pushstring(L, what);
    vs_pushinteger(L, stat);
    return 3;  /* return true/nil,what,code */
  }
}

/* }====================================================== */


/*
** {======================================================
** Argument check functions
** =======================================================
*/

VSLIB_API int vsL_checkoption (vs_State *L, int arg, const char *def,
                                 const char *const lst[]) {
  const char *name = (def) ? vsL_optstring(L, arg, def) :
                             vsL_checkstring(L, arg);
  int i;
  for (i=0; lst[i]; i++)
    if (strcmp(lst[i], name) == 0)
      return i;
  return vsL_argerror(L, arg,
                       vs_pushfstring(L, "invalid option '%s'", name));
}


/*
** Ensures the stack has at least 'space' extra slots, raising an error
** if it cannot fulfill the request. (The error handling needs a few
** extra slots to format the error message. In case of an error without
** this extra space, VS will generate the same 'stack overflow' error,
** but without 'msg'.)
*/
VSLIB_API void vsL_checkstack (vs_State *L, int space, const char *msg) {
  if (!vs_checkstack(L, space)) {
    if (msg)
      vsL_error(L, "stack overflow (%s)", msg);
    else
      vsL_error(L, "stack overflow");
  }
}


VSLIB_API void vsL_checktype (vs_State *L, int arg, int t) {
  if (vs_type(L, arg) != t)
    tag_error(L, arg, t);
}


VSLIB_API void vsL_checkany (vs_State *L, int arg) {
  if (vs_type(L, arg) == VS_TNONE)
    vsL_argerror(L, arg, "value expected");
}


VSLIB_API const char *vsL_checklstring (vs_State *L, int arg, size_t *len) {
  const char *s = vs_tolstring(L, arg, len);
  if (!s) tag_error(L, arg, VS_TSTRING);
  return s;
}


VSLIB_API const char *vsL_optlstring (vs_State *L, int arg,
                                        const char *def, size_t *len) {
  if (vs_isnoneornil(L, arg)) {
    if (len)
      *len = (def ? strlen(def) : 0);
    return def;
  }
  else return vsL_checklstring(L, arg, len);
}


VSLIB_API vs_Number vsL_checknumber (vs_State *L, int arg) {
  int isnum;
  vs_Number d = vs_tonumberx(L, arg, &isnum);
  if (!isnum)
    tag_error(L, arg, VS_TNUMBER);
  return d;
}


VSLIB_API vs_Number vsL_optnumber (vs_State *L, int arg, vs_Number def) {
  return vsL_opt(L, vsL_checknumber, arg, def);
}


static void interror (vs_State *L, int arg) {
  if (vs_isnumber(L, arg))
    vsL_argerror(L, arg, "number has no integer representation");
  else
    tag_error(L, arg, VS_TNUMBER);
}


VSLIB_API vs_Integer vsL_checkinteger (vs_State *L, int arg) {
  int isnum;
  vs_Integer d = vs_tointegerx(L, arg, &isnum);
  if (!isnum) {
    interror(L, arg);
  }
  return d;
}


VSLIB_API vs_Integer vsL_optinteger (vs_State *L, int arg,
                                                      vs_Integer def) {
  return vsL_opt(L, vsL_checkinteger, arg, def);
}

/* }====================================================== */


/*
** {======================================================
** Generic Buffer manipulation
** =======================================================
*/

/* userdata to box arbitrary data */
typedef struct UBox {
  void *box;
  size_t bsize;
} UBox;


static void *resizebox (vs_State *L, int idx, size_t newsize) {
  void *ud;
  vs_Alloc allocf = vs_getallocf(L, &ud);
  UBox *box = (UBox *)vs_touserdata(L, idx);
  void *temp = allocf(ud, box->box, box->bsize, newsize);
  if (temp == NULL && newsize > 0) {  /* allocation error? */
    resizebox(L, idx, 0);  /* free buffer */
    vsL_error(L, "not enough memory for buffer allocation");
  }
  box->box = temp;
  box->bsize = newsize;
  return temp;
}


static void *newbox (vs_State *L, size_t newsize) {
  UBox *box = (UBox *)vs_newuserdata(L, sizeof(UBox));
  box->box = NULL;
  box->bsize = 0;
  return resizebox(L, -1, newsize);
}


/*
** check whether buffer is using a userdata on the stack as a temporary
** buffer
*/
#define buffonstack(B)	((B)->b != (B)->initb)


/*
** returns a pointer to a free area with at least 'sz' bytes
*/
VSLIB_API char *vsL_prepbuffsize (vsL_Buffer *B, size_t sz) {
  vs_State *L = B->L;
  if (B->size - B->n < sz) {  /* not enough space? */
    char *newbuff;
    size_t newsize = B->size * 2;  /* double buffer size */
    if (newsize - B->n < sz)  /* not big enough? */
      newsize = B->n + sz;
    if (newsize < B->n || newsize - B->n < sz)
      vsL_error(L, "buffer too large");
    /* create larger buffer */
    if (buffonstack(B))
      newbuff = (char *)resizebox(L, -1, newsize);
    else {  /* no buffer yet */
      newbuff = (char *)newbox(L, newsize);
      memcpy(newbuff, B->b, B->n * sizeof(char));  /* copy original content */
    }
    B->b = newbuff;
    B->size = newsize;
  }
  return &B->b[B->n];
}


VSLIB_API void vsL_addlstring (vsL_Buffer *B, const char *s, size_t l) {
  if (l > 0) {  /* avoid 'memcpy' when 's' can be NULL */
    char *b = vsL_prepbuffsize(B, l);
    memcpy(b, s, l * sizeof(char));
    vsL_addsize(B, l);
  }
}


VSLIB_API void vsL_addstring (vsL_Buffer *B, const char *s) {
  vsL_addlstring(B, s, strlen(s));
}


VSLIB_API void vsL_pushresult (vsL_Buffer *B) {
  vs_State *L = B->L;
  vs_pushlstring(L, B->b, B->n);
  if (buffonstack(B)) {
    resizebox(L, -2, 0);  /* delete old buffer */
    vs_remove(L, -2);  /* remove its header from the stack */
  }
}


VSLIB_API void vsL_pushresultsize (vsL_Buffer *B, size_t sz) {
  vsL_addsize(B, sz);
  vsL_pushresult(B);
}


VSLIB_API void vsL_addvalue (vsL_Buffer *B) {
  vs_State *L = B->L;
  size_t l;
  const char *s = vs_tolstring(L, -1, &l);
  if (buffonstack(B))
    vs_insert(L, -2);  /* put value below buffer */
  vsL_addlstring(B, s, l);
  vs_remove(L, (buffonstack(B)) ? -2 : -1);  /* remove value */
}


VSLIB_API void vsL_buffinit (vs_State *L, vsL_Buffer *B) {
  B->L = L;
  B->b = B->initb;
  B->n = 0;
  B->size = VSL_BUFFERSIZE;
}


VSLIB_API char *vsL_buffinitsize (vs_State *L, vsL_Buffer *B, size_t sz) {
  vsL_buffinit(L, B);
  return vsL_prepbuffsize(B, sz);
}

/* }====================================================== */


/*
** {======================================================
** Reference system
** =======================================================
*/

/* index of free-list header */
#define freelist	0


VSLIB_API int vsL_ref (vs_State *L, int t) {
  int ref;
  if (vs_isnil(L, -1)) {
    vs_pop(L, 1);  /* remove from stack */
    return VS_REFNIL;  /* 'nil' has a unique fixed reference */
  }
  t = vs_absindex(L, t);
  vs_geti(L, t, freelist);  /* get first free element */
  ref = (int)vs_tointeger(L, -1);  /* ref = t[freelist] */
  vs_pop(L, 1);  /* remove it from stack */
  if (ref != 0) {  /* any free element? */
    vs_geti(L, t, ref);  /* remove it from list */
    vs_seti(L, t, freelist);  /* (t[freelist] = t[ref]) */
  }
  else  /* no free elements */
    ref = (int)vs_rawlen(L, t) + 1;  /* get a new reference */
  vs_seti(L, t, ref);
  return ref;
}


VSLIB_API void vsL_unref (vs_State *L, int t, int ref) {
  if (ref >= 0) {
    t = vs_absindex(L, t);
    vs_geti(L, t, freelist);
    vs_seti(L, t, ref);  /* t[ref] = t[freelist] */
    vs_pushinteger(L, ref);
    vs_seti(L, t, freelist);  /* t[freelist] = ref */
  }
}

/* }====================================================== */


/*
** {======================================================
** Load functions
** =======================================================
*/

typedef struct LoadF {
  int n;  /* number of pre-read characters 预读取的字节数 */
  FILE *f;  /* file being read 要加载读取的文件的文件描述符 */
  // 关于BUFSIZ, 是系统的默认输出缓存大小 输出是1024 ubuntu输出是8192
  // 这里直接利用系统的缓存大小作为文件读取的缓存大小
  char buff[BUFSIZ];  /* area for reading file */
} LoadF;


// 传入LoadF对象 如果有预读取内容返回内容 否则使用fread读取后返回
static const char *getF (vs_State *L, void *ud, size_t *size) {
  LoadF *lf = (LoadF *)ud;
  (void)L;  /* not used */
  if (lf->n > 0) {  /* are there pre-read characters to be read? */
    *size = lf->n;  /* return them (chars already in buffer) */
    lf->n = 0;  /* no more pre-read characters */
  }
  else {  /* read a block from file */
    /* 'fread' can return > 0 *and* set the EOF flag. If next call to
       'getF' called 'fread', it might still wait for user input.
       The next check avoids this problem. */
    if (feof(lf->f)) return NULL;
    *size = fread(lf->buff, 1, sizeof(lf->buff), lf->f);  /* read block */
  }
  return lf->buff;
}


static int errfile (vs_State *L, const char *what, int fnameindex) {
  const char *serr = strerror(errno);
  const char *filename = vs_tostring(L, fnameindex) + 1;
  vs_pushfstring(L, "cannot %s %s: %s", what, filename, serr);
  vs_remove(L, fnameindex);
  return VS_ERRFILE;
}


// BOM——Byte Order Mark，就是字节序标记
// 有BOM 返回BOM后第一个字符
// 无BOM 返回文件第一个字符
// 如果文件头有不足三个字节和BOM一样 读取的字节被放入lf->buff lf->n说明读取的字节数
static int skipBOM (LoadF *lf) {
  const char *p = "\xEF\xBB\xBF";  /* UTF-8 BOM mark */
  int c;
  lf->n = 0;
  do {
    c = getc(lf->f);
    // *p++ 是先得到*p的值再对p自增
    if (c == EOF || c != *(const unsigned char *)p++) return c;
    lf->buff[lf->n++] = c;  /* to be read by the parser */
  } while (*p != '\0');
  lf->n = 0;  /* prefix matched; discard it */
  return getc(lf->f);  /* return next character */
}


/*
** reads the first character of file 'f' and skips an optional BOM mark
** in its beginning plus its first line if it starts with '#'. Returns
** true if it skipped the first line.  In any case, '*cp' has the
** first "valid" character of the file (after the optional BOM and
** a first-line comment).
*/
// 跳过BOM和第一行注释 让cp指向第一个有效字符
// Windows有可能有BOM头 linux可以直接 ./a.vs 运行应该跳过第一行Shebang
static int skipcomment (LoadF *lf, int *cp) {
  // c是文件的第一个有效字符, 即去除BOM后的第一个字符
  int c = *cp = skipBOM(lf);
  if (c == '#') {  /* first line is a comment (Unix exec. file)? */
    do {  /* skip first line */
      c = getc(lf->f);
    } while (c != EOF && c != '\n');
    *cp = getc(lf->f);  /* skip end-of-line, if present */
    return 1;  /* there was a comment */
  }
  else return 0;  /* no comment */
}


// filename为NULL代表stdin
VSLIB_API int vsL_loadfilex (vs_State *L, const char *filename,
                                             const char *mode) {
  LoadF lf;
  int status, readstatus;
  int c;
  // 下面会将文件名压入栈中,所以现在的top+1就是文件的位置
  int fnameindex = vs_gettop(L) + 1;  /* index of filename on the stack */
  if (filename == NULL) { // 如果没有文件名 从stdin中读取
    vs_pushliteral(L, "=stdin");
    lf.f = stdin;
  }
  else {
    vs_pushfstring(L, "@%s", filename);
    lf.f = fopen(filename, "r");
    if (lf.f == NULL) return errfile(L, "open", fnameindex); // 打开文件失败
  }
  if (skipcomment(&lf, &c))  /* read initial portion 处理BOM头和Shebang */
    lf.buff[lf.n++] = '\n';  /* add line to correct line numbers 如果跳过了Shebang行数会不对 所以加一个\n空行 */
  // vsc -o a a.vs 可以生成字节码文件
  if (c == VS_SIGNATURE[0] && filename) {  /* binary file? 文件是预处理过的二进制文件? */
    lf.f = freopen(filename, "rb", lf.f);  /* reopen in binary mode */
    if (lf.f == NULL) return errfile(L, "reopen", fnameindex);
    skipcomment(&lf, &c);  /* re-read initial portion */
  }
  if (c != EOF)
    lf.buff[lf.n++] = c;  /* 'c' is the first character of the stream 将c放入预读取缓存中 */
  // 给vs_load传入的chunkname就是刚才压入的 "filename" 或者 "=stdin"
  status = vs_load(L, getF, &lf, vs_tostring(L, -1), mode);
  readstatus = ferror(lf.f);
  if (filename) fclose(lf.f);  /* close file (even in case of errors) */
  if (readstatus) {
    vs_settop(L, fnameindex);  /* ignore results from 'vs_load' */
    return errfile(L, "read", fnameindex);
  }
  // 删除fnameindex位置的元素
  vs_remove(L, fnameindex);
  return status;
}


typedef struct LoadS {
  const char *s;
  size_t size;
} LoadS;


static const char *getS (vs_State *L, void *ud, size_t *size) {
  LoadS *ls = (LoadS *)ud;
  (void)L;  /* not used */
  if (ls->size == 0) return NULL;
  *size = ls->size;
  ls->size = 0;
  return ls->s;
}


VSLIB_API int vsL_loadbufferx (vs_State *L, const char *buff, size_t size,
                                 const char *name, const char *mode) {
  LoadS ls;
  ls.s = buff;
  ls.size = size;
  return vs_load(L, getS, &ls, name, mode);
}


VSLIB_API int vsL_loadstring (vs_State *L, const char *s) {
  return vsL_loadbuffer(L, s, strlen(s), s);
}

/* }====================================================== */


VSLIB_API vs_Integer vsL_len (vs_State *L, int idx) {
  vs_Integer l;
  int isnum;
  vs_len(L, idx);
  l = vs_tointegerx(L, -1, &isnum);
  if (!isnum)
    vsL_error(L, "object length is not an integer");
  vs_pop(L, 1);  /* remove object */
  return l;
}


// 将不同类型的值转换成字符串 放入栈顶
// 转换规则如下
//  数字类型: 转换成字符串压入栈顶
//  字符串类型: 压入栈顶
//  布尔类型: 转换成true,false 压入栈顶
//  nil: 转换成nil 压入栈顶
//  其他情况: 格式为"%s: %p" 压入栈顶
//    %s部分: 该对象的类型名
//    %p部分: 该对象存储实际数据的地址 如表类型就是Table的地址
VSLIB_API const char *vsL_tolstring (vs_State *L, int idx, size_t *len) {
  switch (vs_type(L, idx)) {
    case VS_TNUMBER: {  // 处理数字类型
      if (vs_isinteger(L, idx))
        vs_pushfstring(L, "%I", (VSI_UACINT)vs_tointeger(L, idx));
      else
        vs_pushfstring(L, "%f", (VSI_UACNUMBER)vs_tonumber(L, idx));
      break;
    }
    case VS_TSTRING:
      vs_pushvalue(L, idx);
      break;
    case VS_TBOOLEAN:
      vs_pushstring(L, (vs_toboolean(L, idx) ? "true" : "false"));
      break;
    case VS_TNIL:  // 如果让nil打印null可以修改这里
      vs_pushliteral(L, "nil");
      break;
    default: {
      const char *kind = vsL_typename(L, idx);
      // 压入要打印的字符串 例如 "table: 0x104efb0"
      // 要让表类型完全打印所有键值对而不是指针可以修改这里
      vs_pushfstring(L, "%s: %p", kind, vs_topointer(L, idx));
      break;
    }
  }
  // 栈顶是数字或字符串 数字进行处理后返回字符串的地址 并修改len
  return vs_tolstring(L, -1, len);
}

/*
** set functions from list 'l' into table at top - 'nup'; each
** function gets the 'nup' elements at the top as upvalues.
** Returns with only the table at the stack.
*/
// 为栈顶的表设置l数组中的键值对, 每个新增的函数都包括同样的n个upvalue
// 执行前栈顶 t n1 n2 n3
// 执行后栈顶 t
VSLIB_API void vsL_setfuncs (vs_State *L, const vsL_Reg *l, int nup) {
  vsL_checkstack(L, nup, "too many upvalues");
  // 进入函数栈顶是用来放入被加载函数的表 t 和 n个upvalue 例如 t n1 n2 n3
  for (; l->name != NULL; l++) {  /* fill the table with given functions */
    int i;
    // 原来栈顶的n个upvalue全部复制一份
    // 例如原来栈顶是 t n1 n2 n3现在变成 t n1 n2 n3 n1 n2 n3
    for (i = 0; i < nup; i++)  /* copy upvalues to the top 压入n个upvalues */
      vs_pushvalue(L, -nup);
    // 压入闭包会清除upvalue 现在栈顶是 t n1 n2 n3 cclosure
    vs_pushcclosure(L, l->func, nup);  /* closure with those upvalues 压入c闭包清除之前的upvalue */
    vs_setfield(L, -(nup + 2), l->name);  // 设置t[l->name] = cclosure 栈顶是 t n1 n2 n3
  }
  vs_pop(L, nup);  /* remove upvalues 删除upvalue 栈顶是 t */
}


/*
** ensure that stack[idx][fname] has a table and push that table
** into the stack
*/
// t是index位置的值
// 确保 t[fname] 的值为一个表, 并将栈顶设置为t[fname]的值
// 如果t[fname]不是表就新建一个表并设置到fname键上, 拷贝新建的表到栈顶
VSLIB_API int vsL_getsubtable (vs_State *L, int idx, const char *fname) {
  if (vs_getfield(L, idx, fname) == VS_TTABLE)  // 将查询的结果压入栈顶
    return 1;  /* table already there */
  else {
    vs_pop(L, 1);  /* remove previous result 前面查询的结果不对 删掉 */
    idx = vs_absindex(L, idx);  // 将负数idx转换成正数idx
    vs_newtable(L);  // 在栈顶新建一个table
    vs_pushvalue(L, -1);  /* copy to be left at top 拷贝一份新建的table 现在栈顶有两个一样的table */
    // setfield会设置完成会删除栈顶 所以这里拷贝了两份 删除一份还能保留一份
    vs_setfield(L, idx, fname);  /* assign new table to field */
    return 0;  /* false, because did not find table there */
  }
}


/*
** Stripped-down 'require': After checking "loaded" table, calls 'openf'
** to open a module, registers the result in 'package.loaded' table and,
** if 'glb' is true, also registers the result in the global table.
** Leaves resulting module on the top.
*/
// 查询modname是否已经加载 如果没有加载就调用openf加载这个模块
// 如果glb为true,那么就设置_G[modname] = module
// 执行后栈顶是openf的返回值
VSLIB_API void vsL_requiref (vs_State *L, const char *modname,
                               vs_CFunction openf, int glb) {
  // VS_LOADED_TABLE "__LOADED"
  // 确保 G(L)->l_registry的 "_LOADED" 键是一个表, 执行后栈顶就是"_LOADED"对应的表
  vsL_getsubtable(L, VS_REGISTRYINDEX, VS_LOADED_TABLE);
  vs_getfield(L, -1, modname);  /* LOADED[modname] 查询LOADED[modname] */
  if (!vs_toboolean(L, -1)) {  /* package not already loaded? 没有查询到该模块已经被加载 */
    vs_pop(L, 1);  /* remove field 删除查询LOADED表的结果 */
    vs_pushcfunction(L, openf);  // 压入打开模块的函数openf
    vs_pushstring(L, modname);  /* argument to open function 压入模块名 */
    // 调用openf函数,openf有一个参数modname
    vs_call(L, 1, 1);  /* call 'openf' to open module 执行openf */
    vs_pushvalue(L, -1);  /* make copy of module (call result) */
    // 现在栈顶情况 LOADED result result  result是函数执行结果
    vs_setfield(L, -3, modname);  /* LOADED[modname] = module */
    // 设置值后栈顶是 LOADED result
  }
  vs_remove(L, -2);  /* remove LOADED table 删除LOADED表 现在栈顶是result */
  if (glb) {
    vs_pushvalue(L, -1);  /* copy of module */
    vs_setglobal(L, modname);  /* _G[modname] = module */
  }
}


VSLIB_API const char *vsL_gsub (vs_State *L, const char *s, const char *p,
                                                               const char *r) {
  const char *wild;
  size_t l = strlen(p);
  vsL_Buffer b;
  vsL_buffinit(L, &b);
  while ((wild = strstr(s, p)) != NULL) {
    vsL_addlstring(&b, s, wild - s);  /* push prefix */
    vsL_addstring(&b, r);  /* push replacement in place of pattern */
    s = wild + l;  /* continue after 'p' */
  }
  vsL_addstring(&b, s);  /* push last suffix */
  vsL_pushresult(&b);
  return vs_tostring(L, -1);
}


// 实际的内存分配函数
static void *l_alloc (void *ud, void *ptr, size_t osize, size_t nsize) {
  (void)ud; (void)osize;  /* not used */
  if (nsize == 0) {
    free(ptr);
    return NULL;
  }
  else
    return realloc(ptr, nsize);
}


static int panic (vs_State *L) {
  vs_writestringerror("PANIC: unprotected error in call to VS API (%s)\n",
                        vs_tostring(L, -1));
  return 0;  /* return to VS to abort */
}


VSLIB_API vs_State *vsL_newstate (void) {
  vs_State *L = vs_newstate(l_alloc, NULL);
  if (L) vs_atpanic(L, &panic);
  return L;
}

