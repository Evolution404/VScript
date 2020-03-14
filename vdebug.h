#ifndef vdebug_h
#define vdebug_h


#include "vstate.h"


#define pcRel(pc, p)	(cast(int, (pc) - (p)->code) - 1)

#define getfuncline(f,pc)	(((f)->lineinfo) ? (f)->lineinfo[pc] : -1)


VSI_FUNC l_noret vsG_typeerror (vs_State *L, const TValue *o,
                                                const char *opname);
VSI_FUNC l_noret vsG_concaterror (vs_State *L, const TValue *p1,
                                                  const TValue *p2);
VSI_FUNC l_noret vsG_opinterror (vs_State *L, const TValue *p1,
                                                 const TValue *p2,
                                                 const char *msg);
VSI_FUNC l_noret vsG_tointerror (vs_State *L, const TValue *p1,
                                                 const TValue *p2);
VSI_FUNC l_noret vsG_ordererror (vs_State *L, const TValue *p1,
                                                 const TValue *p2);
VSI_FUNC l_noret vsG_runerror (vs_State *L, const char *fmt, ...);
VSI_FUNC const char *vsG_addinfo (vs_State *L, const char *msg,
                                                  TString *src, int line);
VSI_FUNC l_noret vsG_errormsg (vs_State *L);

#endif
