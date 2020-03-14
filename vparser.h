#ifndef vparser_h
#define vparser_h

#include "vlimits.h"
#include "vobject.h"
#include "vzio.h"


// 表达式和变量的描述符
// 对于变量和表达式的代码生成器可以被推迟以便于进行优化
// expdesc结构体描述了一个潜在的可被推迟的变量或表达式
// 它有一个它的主要值的描述信息加上以一系列可以生成它的值的条件跳转
// (由短路操作符and/or生成)

/* kinds of variables/expressions */
// 变量或表达式的类型
typedef enum {
  VVOID,  /* when 'expdesc' describes the last expression a list,
             this kind means an empty list (so, no expression) */
  // 常量nil
  VNIL,  /* constant nil */
  // 常量true
  VTRUE,  /* constant true */
  // 常量false
  VFALSE,  /* constant false */
  // 常量k info代表常量表的下标
  VK,  /* constant in 'k'; info = index of constant in 'k' */
  // 浮点数常量
  VKFLT,  /* floating constant; nval = numerical float value */
  // 整数常量
  VKINT,  /* integer constant; nval = numerical integer value */
  // 具有固定寄存器位置的表达式 info代表了结果所在的寄存器 
  VNONRELOC,  /* expression has its value in a fixed register;
                 info = result register */
  // 局部变量表达式 在info中保存局部变量对应的寄存器id
  VLOCAL,  /* local variable; info = local register */
  // upvalue变量表达式,在info中保存upvalue的id
  VUPVAL,  /* upvalue variable; info = index of upvalue in 'upvalues' */
  // 代表对一个表进行索引的变量表达式,如a.b或者a[1]
  // idx保存用来索引的key的id 可能是寄存器id或者常量id
  // t保存被索引的表的id 可能是寄存器id或者upvalue id
  // vt表示t的类型是寄存器id(VLOCAL)(8)还是upvalue id(VUPVAL)(9)
  VINDEXED,  /* indexed variable;
                ind.vt = whether 't' is register or upvalue;
                ind.t = table register or upvalue;
                ind.idx = key's R/K index */
  // 测试或比较的表达式 info存放对应跳转指令的位置
  // 包括一条测试指令和OP_JMP指令,测试指令为真执行OP_JMP指令
  // 也就是goiffalse
  VJMP,  /* expression is a test/comparison;
            info = pc of corresponding jump instruction */
  // 可以放在任何寄存器位置的表达式 info代表了需要回填参数A的指令位置
  VRELOCABLE,  /* expression can put result in any register;
                  info = instruction pc */
  // 函数调用表达式, info代表OP_CALL的位置
  VCALL,  /* expression is a function call; info = instruction pc */
  // 可变长度表达式
  VVARARG  /* vararg expression; info = instruction pc */
} expkind;


// 测试k是不是变量 局部变量 upvalue indexed三种
#define vkisvar(k)	(VLOCAL <= (k) && (k) <= VINDEXED)
// 测试k是否在寄存器中
#define vkisinreg(k)	((k) == VNONRELOC || (k) == VLOCAL)

// 表达式描述信息
typedef struct expdesc {
  expkind k;
  union {
    vs_Integer ival;    /* for VKINT */
    vs_Number nval;  /* for VKFLT */
    // 代表常量表下标,upvalue表下标或者OP_JMP指令的下标
    int info;  /* for generic use */
    // ind在类型为VINDEXED时有效
    struct {  /* for indexed variables (VINDEXED) */
      // 保存在寄存器也就是临时变量或者是局部变量
      // 查询的key保存在寄存器或常量(RK)中
      short idx;  /* index (R/K) */
      // 被查询的表,保存在寄存器或者upvalue中
      lu_byte t;  /* table (register or upvalue) */
      // vt取值为VLOCAL或者VUPVAL,用来标记t是保存在寄存器还是upvalue中
      // idx不需要额外的变量标记,RK可以根据倒数第8位是否为1来区分
      lu_byte vt;  /* whether 't' is register (VLOCAL) or upvalue (VUPVAL) */
    } ind;
  } u;
  // t和f是回填链 把跳转目标地址相同的一些指令串在一起
  // 确定了跳转位置后遍历这个链填充上跳转目标地址
  int t;  /* patch list of 'exit when true' */
  int f;  /* patch list of 'exit when false' */
} expdesc;


/* description of active local variable */
// 活跃局部变量的描述信息
typedef struct Vardesc {
  short idx;  /* variable index in stack */
} Vardesc;


// goto和label是用来执行非条件跳转的statement 这两个statement分别在gotostat和labelstat函数中被解析
// goto和label使用一个相同的数据结构Labeldesc表示

/* description of pending goto statements and label statements */
// 等待goto语句和label语句的描述符
typedef struct Labeldesc {
  // 用来表示label的名称,用来相互查找
  TString *name;  /* label identifier */
  // pc对于label和goto表示不同含义
  //   如果是label,pc表示这个label对应的goto要跳转的位置
  //   如果是goto,pc则代表为这个goto生成的OP_JMP指令的位置
  int pc;  /* position in code */
  int line;  /* line where it appeared */
  // 代表解析此goto或者label时,函数有多少个有效的局部变量,用来在跳转时决定需要关闭哪些upvalue
  lu_byte nactvar;  /* local level where it appears in current block */
} Labeldesc;


/* list of labels or gotos */
// label或goto的列表
typedef struct Labellist {
  Labeldesc *arr;  /* array 实际存放位置 */
  int n;  /* number of entries in use 数组长度 */
  int size;  /* array size 数组空间大小 */
} Labellist;


/* dynamic structures used by the parser */
// 分析过程中只有唯一一个Dyndata对象
// 存放局部变量列表 goto列表和label列表
// 分析到每一个块内时该对象保存了这个块内此时所有有效的局部变量,goto,label
typedef struct Dyndata {
  struct {  /* list of active local variables 活跃局部变量的列表 */
    Vardesc *arr;
    int n;
    int size;
  } actvar;
  // goto列表不是栈结构,保存的是未成功匹配的goto语句
  Labellist gt;  /* list of pending gotos goto的列表 */
  // label列表是栈结构,表示在当前块所有有效的label
  // 也就是每定义一个label在栈中增加一个,退出一个块栈中移除这个块定义的label
  Labellist label;   /* list of active labels label的列表 */
} Dyndata;


/* control of blocks 块控制 */
struct BlockCnt;  /* defined in lparser.c */


/* state needed to generate code for a given function */
// 在编译过程中,使用FuncState结构体来保存一个函数编译的状态数据
// FuncState只是用来保存一个中间状态,目的是生成最终结果Proto对象,也就是f
// 多个FuncState构成栈结构,类似深度优先搜索,当前分析函数和其父函数串成一条链构成栈
// 当前函数分析完毕,栈顶弹出当前函数,变成父函数,如果父函数还有子函数那么继续分析将子函数压入栈
typedef struct FuncState {
  Proto *f;  /* current function header */
  // 指向当前函数的父函数
  struct FuncState *prev;  /* enclosing function */
  struct LexState *ls;  /* lexical state */
  // 当前所处的块
  struct BlockCnt *bl;  /* chain of current blocks */
  // 下一条生成指令的下标 就是现在已经有的指令条数
  int pc;  /* next position to code (equivalent to 'ncode') */
  // 保存上一次跳转链回填的指令位置
  int lasttarget;   /* 'label' of last 'jump label' */
  // 使用下一个待生成指令地址回填的回填链
  int jpc;  /* list of pending jumps to 'pc' */
  int nk;  /* number of elements in 'k' */
  int np;  /* number of elements in 'p' */
  int firstlocal;  /* index of first local var (in Dyndata array) */
  short nlocvars;  /* number of elements in 'f->locvars' */
  lu_byte nactvar;  /* number of active local variables */
  lu_byte nups;  /* number of upvalues */
  lu_byte freereg;  /* first free register 第一个空闲的寄存器 */
} FuncState;


// 暴露出来的唯一的函数,用来驱动分析器执行
VSI_FUNC LClosure *vsY_parser (vs_State *L, ZIO *z, Mbuffer *buff,
                                 Dyndata *dyd, const char *name, int firstchar);


#endif
