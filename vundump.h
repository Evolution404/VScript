#ifndef vundump_h
#define vundump_h

#include "vlimits.h"
#include "vobject.h"
#include "vzio.h"


/* data to catch conversion errors */
#define VSC_DATA	"\x19\x93\r\n\x1a\n"

#define VSC_INT	0x5678
#define VSC_NUM	cast_num(370.5)

#define MYINT(s)	(s[0]-'0')
#define VSC_VERSION	(MYINT(VS_VERSION_MAJOR)*16+MYINT(VS_VERSION_MINOR))
#define VSC_FORMAT	0	/* this is the official format */

/* load one chunk; from lundump.c */
VSI_FUNC LClosure* vsU_undump (vs_State* L, ZIO* Z, const char* name);

/* dump one chunk; from ldump.c */
VSI_FUNC int vsU_dump (vs_State* L, const Proto* f, vs_Writer w,
                         void* data, int strip);

#endif
