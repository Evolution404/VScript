#ifndef vauxlib_h
#define vauxlib_h


#include <stddef.h>
#include <stdio.h>

#include "vs.h"



/* extra error code for 'vsL_loadfilex' */
#define VS_ERRFILE     (VS_ERRERR+1)


/* key, in the registry, for table of loaded modules */
#define VS_LOADED_TABLE	"_LOADED"


/* key, in the registry, for table of preloaded loaders */
#define VS_PRELOAD_TABLE	"_PRELOAD"


// 描述模块的结构体
typedef struct vsL_Reg {
  const char *name;   // 模块名
  vs_CFunction func; // 模块初始化函数
} vsL_Reg;


#define VSL_NUMSIZES	(sizeof(vs_Integer)*16 + sizeof(vs_Number))

VSLIB_API const char *(vsL_tolstring) (vs_State *L, int idx, size_t *len);
VSLIB_API int (vsL_argerror) (vs_State *L, int arg, const char *extramsg);
VSLIB_API const char *(vsL_checklstring) (vs_State *L, int arg,
                                                          size_t *l);
VSLIB_API const char *(vsL_optlstring) (vs_State *L, int arg,
                                          const char *def, size_t *l);
VSLIB_API vs_Number (vsL_checknumber) (vs_State *L, int arg);
VSLIB_API vs_Number (vsL_optnumber) (vs_State *L, int arg, vs_Number def);

VSLIB_API vs_Integer (vsL_checkinteger) (vs_State *L, int arg);
VSLIB_API vs_Integer (vsL_optinteger) (vs_State *L, int arg,
                                          vs_Integer def);

VSLIB_API void (vsL_checkstack) (vs_State *L, int sz, const char *msg);
VSLIB_API void (vsL_checktype) (vs_State *L, int arg, int t);
VSLIB_API void (vsL_checkany) (vs_State *L, int arg);

VSLIB_API void *(vsL_testudata) (vs_State *L, int ud, const char *tname);
VSLIB_API void *(vsL_checkudata) (vs_State *L, int ud, const char *tname);

VSLIB_API void (vsL_where) (vs_State *L, int lvl);
VSLIB_API int (vsL_error) (vs_State *L, const char *fmt, ...);

VSLIB_API int (vsL_checkoption) (vs_State *L, int arg, const char *def,
                                   const char *const lst[]);

VSLIB_API int (vsL_fileresult) (vs_State *L, int stat, const char *fname);
VSLIB_API int (vsL_execresult) (vs_State *L, int stat);

/* predefined references */
#define VS_NOREF       (-2)
#define VS_REFNIL      (-1)

VSLIB_API int (vsL_ref) (vs_State *L, int t);
VSLIB_API void (vsL_unref) (vs_State *L, int t, int ref);

VSLIB_API int (vsL_loadfilex) (vs_State *L, const char *filename,
                                               const char *mode);

#define vsL_loadfile(L,f)	vsL_loadfilex(L,f,NULL)

VSLIB_API int (vsL_loadbufferx) (vs_State *L, const char *buff, size_t sz,
                                   const char *name, const char *mode);
VSLIB_API int (vsL_loadstring) (vs_State *L, const char *s);

VSLIB_API vs_State *(vsL_newstate) (void);

VSLIB_API vs_Integer (vsL_len) (vs_State *L, int idx);

VSLIB_API const char *(vsL_gsub) (vs_State *L, const char *s, const char *p,
                                                  const char *r);

VSLIB_API void (vsL_setfuncs) (vs_State *L, const vsL_Reg *l, int nup);

VSLIB_API int (vsL_getsubtable) (vs_State *L, int idx, const char *fname);

VSLIB_API void (vsL_traceback) (vs_State *L, vs_State *L1,
                                  const char *msg, int level);

VSLIB_API void (vsL_requiref) (vs_State *L, const char *modname,
                                 vs_CFunction openf, int glb);

/*
** ===============================================================
** some useful macros
** ===============================================================
*/


#define vsL_newlibtable(L,l)	\
  vs_createtable(L, 0, sizeof(l)/sizeof((l)[0]) - 1)

#define vsL_newlib(L,l)  \
  (vsL_newlibtable(L,l), vsL_setfuncs(L,l,0))

#define vsL_argcheck(L, cond,arg,extramsg)	\
		((void)((cond) || vsL_argerror(L, (arg), (extramsg))))
#define vsL_checkstring(L,n)	(vsL_checklstring(L, (n), NULL))
#define vsL_optstring(L,n,d)	(vsL_optlstring(L, (n), (d), NULL))

#define vsL_typename(L,i)	vs_typename(L, vs_type(L,(i)))

// 当vsL_loadfile返回VS_OK也就是0的时候,执行vs_pcall
#define vsL_dofile(L, fn) \
	(vsL_loadfile(L, fn) || vs_pcall(L, 0, VS_MULTRET, 0))

#define vsL_dostring(L, s) \
	(vsL_loadstring(L, s) || vs_pcall(L, 0, VS_MULTRET, 0))


#define vsL_opt(L,f,n,d)	(vs_isnoneornil(L,(n)) ? (d) : f(L,(n)))

#define vsL_loadbuffer(L,s,sz,n)	vsL_loadbufferx(L,s,sz,n,NULL)


/*
** {======================================================
** Generic Buffer manipulation
** =======================================================
*/

typedef struct vsL_Buffer {
  char *b;  /* buffer address */
  size_t size;  /* buffer size */
  size_t n;  /* number of characters in buffer */
  vs_State *L;
  char initb[VSL_BUFFERSIZE];  /* initial buffer */
} vsL_Buffer;


#define vsL_addchar(B,c) \
  ((void)((B)->n < (B)->size || vsL_prepbuffsize((B), 1)), \
   ((B)->b[(B)->n++] = (c)))

#define vsL_addsize(B,s)	((B)->n += (s))

VSLIB_API void (vsL_buffinit) (vs_State *L, vsL_Buffer *B);
VSLIB_API char *(vsL_prepbuffsize) (vsL_Buffer *B, size_t sz);
VSLIB_API void (vsL_addlstring) (vsL_Buffer *B, const char *s, size_t l);
VSLIB_API void (vsL_addstring) (vsL_Buffer *B, const char *s);
VSLIB_API void (vsL_addvalue) (vsL_Buffer *B);
VSLIB_API void (vsL_pushresult) (vsL_Buffer *B);
VSLIB_API void (vsL_pushresultsize) (vsL_Buffer *B, size_t sz);
VSLIB_API char *(vsL_buffinitsize) (vs_State *L, vsL_Buffer *B, size_t sz);

#define vsL_prepbuffer(B)	vsL_prepbuffsize(B, VSL_BUFFERSIZE)

/* }====================================================== */



/*
** {======================================================
** File handles for IO library
** =======================================================
*/

/*
** A file handle is a userdata with metatable 'VS_FILEHANDLE' and
** initial structure 'vsL_Stream' (it may contain other fields
** after that initial structure).
*/

#define VS_FILEHANDLE          "FILE*"


typedef struct vsL_Stream {
  FILE *f;  /* stream (NULL for incompletely created streams) */
  vs_CFunction closef;  /* to close stream (NULL for closed streams) */
} vsL_Stream;

/* }====================================================== */





/*
** {==================================================================
** "Abstraction Layer" for basic report of messages and errors
** ===================================================================
*/

/* print a string */
// 打印一个字符串,s代表字符串,l代表字符串长度
#if !defined(vs_writestring)
#define vs_writestring(s,l)   fwrite((s), sizeof(char), (l), stdout)
#endif

/* print a newline and flush the output */
// 用来打印一个\n
#if !defined(vs_writeline)
#define vs_writeline()        (vs_writestring("\n", 1), fflush(stdout))
#endif

/* print an error message */
#if !defined(vs_writestringerror)
#define vs_writestringerror(s,p) \
        (fprintf(stderr, (s), (p)), fflush(stderr))
#endif

/* }================================================================== */


#endif


