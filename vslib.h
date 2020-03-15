#ifndef vslib_h
#define vslib_h

#include "vs.h"


/* version suffix for environment variable names */
#define VS_VERSUFFIX          "_" VS_VERSION_MAJOR "_" VS_VERSION_MINOR


VSMOD_API int (vsopen_base) (vs_State *L);

#define VS_TABLIBNAME	"table"
VSMOD_API int (vsopen_table) (vs_State *L);

//#define VS_IOLIBNAME	"io"
//VSMOD_API int (vsopen_io) (vs_State *L);
//
//#define VS_OSLIBNAME	"os"
//VSMOD_API int (vsopen_os) (vs_State *L);
//
//#define VS_STRLIBNAME	"string"
//VSMOD_API int (vsopen_string) (vs_State *L);
//
//#define VS_MATHLIBNAME	"math"
//VSMOD_API int (vsopen_math) (vs_State *L);

/* open all previous libraries */
VSLIB_API void (vsL_openlibs) (vs_State *L);


#if !defined(vs_assert)
#define vs_assert(x)	((void)0)
#endif


#endif
