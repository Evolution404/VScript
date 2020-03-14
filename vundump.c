#define lundump_c
#define VS_CORE

#include <string.h>

#include "vs.h"

#include "vdebug.h"
#include "vdo.h"
#include "vfunc.h"
#include "vmem.h"
#include "vobject.h"
#include "vstring.h"
#include "vundump.h"
#include "vzio.h"


#if !defined(vsi_verifycode)
#define vsi_verifycode(L,b,f)  /* empty */
#endif


typedef struct {
  vs_State *L;
  ZIO *Z;
  const char *name;
} LoadState;


static l_noret error(LoadState *S, const char *why) {
  vsO_pushfstring(S->L, "%s: %s precompiled chunk", S->name, why);
  vsD_throw(S->L, VS_ERRSYNTAX);
}


/*
** All high-level loads go through LoadVector; you can change it to
** adapt to the endianness of the input
*/
#define LoadVector(S,b,n)	LoadBlock(S,b,(n)*sizeof((b)[0]))

static void LoadBlock (LoadState *S, void *b, size_t size) {
  if (vsZ_read(S->Z, b, size) != 0)
    error(S, "truncated");
}


#define LoadVar(S,x)		LoadVector(S,&x,1)


static lu_byte LoadByte (LoadState *S) {
  lu_byte x;
  LoadVar(S, x);
  return x;
}


static int LoadInt (LoadState *S) {
  int x;
  LoadVar(S, x);
  return x;
}


static vs_Number LoadNumber (LoadState *S) {
  vs_Number x;
  LoadVar(S, x);
  return x;
}


static vs_Integer LoadInteger (LoadState *S) {
  vs_Integer x;
  LoadVar(S, x);
  return x;
}


static TString *LoadString (LoadState *S) {
  size_t size = LoadByte(S);
  if (size == 0xFF)
    LoadVar(S, size);
  if (size == 0)
    return NULL;
  else if (--size <= VSI_MAXSHORTLEN) {  /* short string? */
    char buff[VSI_MAXSHORTLEN];
    LoadVector(S, buff, size);
    return vsS_newlstr(S->L, buff, size);
  }
  else {  /* long string */
    TString *ts = vsS_createlngstrobj(S->L, size);
    LoadVector(S, getstr(ts), size);  /* load directly in final place */
    return ts;
  }
}


static void LoadCode (LoadState *S, Proto *f) {
  int n = LoadInt(S);
  f->code = vsM_newvector(S->L, n, Instruction);
  f->sizecode = n;
  LoadVector(S, f->code, n);
}


static void LoadFunction(LoadState *S, Proto *f, TString *psource);


static void LoadConstants (LoadState *S, Proto *f) {
  int i;
  int n = LoadInt(S);
  f->k = vsM_newvector(S->L, n, TValue);
  f->sizek = n;
  for (i = 0; i < n; i++)
    setnilvalue(&f->k[i]);
  for (i = 0; i < n; i++) {
    TValue *o = &f->k[i];
    int t = LoadByte(S);
    switch (t) {
    case VS_TNIL:
      setnilvalue(o);
      break;
    case VS_TBOOLEAN:
      setbvalue(o, LoadByte(S));
      break;
    case VS_TNUMFLT:
      setfltvalue(o, LoadNumber(S));
      break;
    case VS_TNUMINT:
      setivalue(o, LoadInteger(S));
      break;
    case VS_TSHRSTR:
    case VS_TLNGSTR:
      setsvalue2n(S->L, o, LoadString(S));
      break;
    default:
      vs_assert(0);
    }
  }
}


static void LoadProtos (LoadState *S, Proto *f) {
  int i;
  int n = LoadInt(S);
  f->p = vsM_newvector(S->L, n, Proto *);
  f->sizep = n;
  for (i = 0; i < n; i++)
    f->p[i] = NULL;
  for (i = 0; i < n; i++) {
    f->p[i] = vsF_newproto(S->L);
    LoadFunction(S, f->p[i], f->source);
  }
}


static void LoadUpvalues (LoadState *S, Proto *f) {
  int i, n;
  n = LoadInt(S);
  f->upvalues = vsM_newvector(S->L, n, Upvaldesc);
  f->sizeupvalues = n;
  for (i = 0; i < n; i++)
    f->upvalues[i].name = NULL;
  for (i = 0; i < n; i++) {
    f->upvalues[i].instack = LoadByte(S);
    f->upvalues[i].idx = LoadByte(S);
  }
}


static void LoadDebug (LoadState *S, Proto *f) {
  int i, n;
  n = LoadInt(S);
  f->lineinfo = vsM_newvector(S->L, n, int);
  f->sizelineinfo = n;
  LoadVector(S, f->lineinfo, n);
  n = LoadInt(S);
  f->locvars = vsM_newvector(S->L, n, LocVar);
  f->sizelocvars = n;
  for (i = 0; i < n; i++)
    f->locvars[i].varname = NULL;
  for (i = 0; i < n; i++) {
    f->locvars[i].varname = LoadString(S);
    f->locvars[i].startpc = LoadInt(S);
    f->locvars[i].endpc = LoadInt(S);
  }
  n = LoadInt(S);
  for (i = 0; i < n; i++)
    f->upvalues[i].name = LoadString(S);
}


static void LoadFunction (LoadState *S, Proto *f, TString *psource) {
  f->source = LoadString(S);
  if (f->source == NULL)  /* no source in dump? */
    f->source = psource;  /* reuse parent's source */
  f->linedefined = LoadInt(S);
  f->lastlinedefined = LoadInt(S);
  f->numparams = LoadByte(S);
  f->is_vararg = LoadByte(S);
  f->maxstacksize = LoadByte(S);
  LoadCode(S, f);
  LoadConstants(S, f);
  LoadUpvalues(S, f);
  LoadProtos(S, f);
  LoadDebug(S, f);
}


static void checkliteral (LoadState *S, const char *s, const char *msg) {
  char buff[sizeof(VS_SIGNATURE) + sizeof(VSC_DATA)]; /* larger than both */
  size_t len = strlen(s);
  LoadVector(S, buff, len);
  if (memcmp(s, buff, len) != 0)
    error(S, msg);
}


static void fchecksize (LoadState *S, size_t size, const char *tname) {
  if (LoadByte(S) != size)
    error(S, vsO_pushfstring(S->L, "%s size mismatch in", tname));
}


#define checksize(S,t)	fchecksize(S,sizeof(t),#t)

static void checkHeader (LoadState *S) {
  checkliteral(S, VS_SIGNATURE + 1, "not a");  /* 1st char already checked */
  if (LoadByte(S) != VSC_VERSION)
    error(S, "version mismatch in");
  if (LoadByte(S) != VSC_FORMAT)
    error(S, "format mismatch in");
  checkliteral(S, VSC_DATA, "corrupted");
  checksize(S, int);
  checksize(S, size_t);
  checksize(S, Instruction);
  checksize(S, vs_Integer);
  checksize(S, vs_Number);
  if (LoadInteger(S) != VSC_INT)
    error(S, "endianness mismatch in");
  if (LoadNumber(S) != VSC_NUM)
    error(S, "float format mismatch in");
}


/*
** load precompiled chunk
*/
LClosure *vsU_undump(vs_State *L, ZIO *Z, const char *name) {
  LoadState S;
  LClosure *cl;
  if (*name == '@' || *name == '=')
    S.name = name + 1;
  else if (*name == VS_SIGNATURE[0])
    S.name = "binary string";
  else
    S.name = name;
  S.L = L;
  S.Z = Z;
  checkHeader(&S);
  cl = vsF_newLclosure(L, LoadByte(&S));
  setclLvalue(L, L->top, cl);
  vsD_inctop(L);
  cl->p = vsF_newproto(L);
  LoadFunction(&S, cl->p, NULL);
  vs_assert(cl->nupvalues == cl->p->sizeupvalues);
  vsi_verifycode(L, buff, cl->p);
  return cl;
}

