#ifndef vcode_h
#define vcode_h

#include "vlex.h"
#include "vobject.h"
#include "vopcodes.h"
#include "vparser.h"


/*
** Marks the end of a patch list. It is an invalid value both as an absolute
** address, and as a list link (would link an element to itself).
*/
// 标记truelist和falselist的结束位置
#define NO_JUMP (-1)


/*
** grep "ORDER OPR" if you change these enums  (ORDER OP)
*/
// 二元操作符(binary operator)
typedef enum BinOpr {
  OPR_ADD, OPR_SUB, OPR_MUL, OPR_MOD, OPR_POW,
  OPR_DIV,
  OPR_IDIV,
  OPR_BAND, OPR_BOR, OPR_BXOR,
  OPR_SHL, OPR_SHR,
  OPR_CONCAT,
  OPR_EQ, OPR_LT, OPR_LE,
  OPR_NE, OPR_GT, OPR_GE,
  OPR_AND, OPR_OR,
  OPR_NOBINOPR
} BinOpr;


// 一元操作符(unary operator)
// 对应的操作符           -          ~        not       #
typedef enum UnOpr { OPR_MINUS, OPR_BNOT, OPR_NOT, OPR_LEN, OPR_NOUNOPR } UnOpr;


/* get (pointer to) instruction of given 'expdesc' */
// 查询表达式对应的字节码
#define getinstruction(fs,e)	((fs)->f->code[(e)->u.info])

// 构建iAsBx类型的指令利用codeABx函数
// sBx加上MAXARG_sBx 利用这种方式用无符号类型来表示有符号数
#define vsK_codeAsBx(fs,o,A,sBx)	vsK_codeABx(fs,o,A,(sBx)+MAXARG_sBx)

#define vsK_setmultret(fs,e)	vsK_setreturns(fs, e, VS_MULTRET)

#define vsK_jumpto(fs,t)	vsK_patchlist(fs, vsK_jump(fs), t)

VSI_FUNC int vsK_codeABx (FuncState *fs, OpCode o, int A, unsigned int Bx);
VSI_FUNC int vsK_codeABC (FuncState *fs, OpCode o, int A, int B, int C);
VSI_FUNC int vsK_codek (FuncState *fs, int reg, int k);
VSI_FUNC void vsK_fixline (FuncState *fs, int line);
VSI_FUNC void vsK_nil (FuncState *fs, int from, int n);
VSI_FUNC void vsK_reserveregs (FuncState *fs, int n);
VSI_FUNC void vsK_checkstack (FuncState *fs, int n);
VSI_FUNC int vsK_stringK (FuncState *fs, TString *s);
VSI_FUNC int vsK_intK (FuncState *fs, vs_Integer n);
VSI_FUNC void vsK_dischargevars (FuncState *fs, expdesc *e);
VSI_FUNC int vsK_exp2anyreg (FuncState *fs, expdesc *e);
VSI_FUNC void vsK_exp2anyregup (FuncState *fs, expdesc *e);
VSI_FUNC void vsK_exp2nextreg (FuncState *fs, expdesc *e);
VSI_FUNC void vsK_exp2val (FuncState *fs, expdesc *e);
VSI_FUNC int vsK_exp2RK (FuncState *fs, expdesc *e);
VSI_FUNC void vsK_self (FuncState *fs, expdesc *e, expdesc *key);
VSI_FUNC void vsK_indexed (FuncState *fs, expdesc *t, expdesc *k);
VSI_FUNC void vsK_goiftrue (FuncState *fs, expdesc *e);
VSI_FUNC void vsK_goiffalse (FuncState *fs, expdesc *e);
VSI_FUNC void vsK_storevar (FuncState *fs, expdesc *var, expdesc *e);
VSI_FUNC void vsK_setreturns (FuncState *fs, expdesc *e, int nresults);
VSI_FUNC void vsK_setoneret (FuncState *fs, expdesc *e);
VSI_FUNC int vsK_jump (FuncState *fs);
VSI_FUNC void vsK_ret (FuncState *fs, int first, int nret);
VSI_FUNC void vsK_patchlist (FuncState *fs, int list, int target);
VSI_FUNC void vsK_patchtohere (FuncState *fs, int list);
VSI_FUNC void vsK_patchclose (FuncState *fs, int list, int level);
VSI_FUNC void vsK_concat (FuncState *fs, int *l1, int l2);
VSI_FUNC int vsK_getlabel (FuncState *fs);
VSI_FUNC void vsK_prefix (FuncState *fs, UnOpr op, expdesc *v, int line);
VSI_FUNC void vsK_infix (FuncState *fs, BinOpr op, expdesc *v);
VSI_FUNC void vsK_posfix (FuncState *fs, BinOpr op, expdesc *v1,
                            expdesc *v2, int line);
VSI_FUNC void vsK_setlist (FuncState *fs, int base, int nelems, int tostore);


#endif
