#define vtable_c

#include <math.h>
#include <limits.h>

#include "vs.h"

#include "vdebug.h"
#include "vdo.h"
#include "vgc.h"
#include "vmem.h"
#include "vobject.h"
#include "vstate.h"
#include "vstring.h"
#include "vtable.h"
#include "vvm.h"


// 需要确保数组部分和哈希表部分加在一起能够在unsigned int范围内
/*
** Maximum size of array part (MAXASIZE) is 2^MAXABITS. MAXABITS is
** the largest integer such that MAXASIZE fits in an unsigned int.
*/
#define MAXABITS	cast_int(sizeof(int) * CHAR_BIT - 1)  // CHAR_BIT=8 一个字节
// 数组部分的最大大小
#define MAXASIZE	(1u << MAXABITS)

/*
** Maximum size of hash part is 2^MAXHBITS. MAXHBITS is the largest
** integer such that 2^MAXHBITS fits in a signed int. (Note that the
** maximum number of elements in a table, 2^MAXABITS + 2^MAXHBITS, still
** fits comfortably in an unsigned int.)
*/
// 哈希表部分的最大大小
#define MAXHBITS	(MAXABITS - 1)


// 对2的指数取余的方式计算桶位置
// n是哈希值 通过哈希值查询表t中节点
#define hashpow2(t,n)		(gnode(t, lmod((n), sizenode(t))))

#define hashstr(t,str)		hashpow2(t, (str)->hash)
#define hashboolean(t,p)	hashpow2(t, p)
#define hashint(t,i)		hashpow2(t, i)


/*
** for some types, it is better to avoid modulus by power of 2, as
** they tend to have many 2 factors.
*/
// 对于一些类型使用对2的指数取余方式来得到桶位置不合适因为有太多2的因子 例如指针
// 使用对桶个数-1取余, 避免碰撞率太高
#define hashmod(t,n)	(gnode(t, ((n) % ((sizenode(t)-1)|1))))


// 计算指针桶位置方式
#define hashpointer(t,p)	hashmod(t, point2uint(p))


#define dummynode		(&dummynode_)

// 初始化一个空节点
static const Node dummynode_ = {
  {NILCONSTANT},  /* value */
  {{NILCONSTANT, 0}}  /* key */
};


// 计算浮点数的哈希值
// double frexp(double x, int *exp);
// frexp用来把一个浮点数分解为指数和尾数 ret是返回值是尾数 exp是分解后的指数
// 三者之间有关系如下关系 x = ret * 2 ^ exp
static int l_hashfloat (vs_Number n) {
  int i;
  vs_Integer ni;
  n = l_mathop(frexp)(n, &i) * -cast_num(INT_MIN);
  if (!vs_numbertointeger(n, &ni)) {  /* is 'n' inf/-inf/NaN? */
    vs_assert(vsi_numisnan(n) || l_mathop(fabs)(n) == cast_num(HUGE_VAL));
    return 0;
  }
  else {  /* normal case */
    unsigned int u = cast(unsigned int, i) + cast(unsigned int, ni);
    return cast_int(u <= cast(unsigned int, INT_MAX) ? u : ~u);
  }
}


/*
** returns the 'main' position of an element in a table (that is, the index
** of its hash value)
*/
// 输入各种类型key 计算该key应该在的桶的位置
static Node *mainposition (const Table *t, const TValue *key) {
  switch (ttype(key)) {
    case VS_TNUMINT:
      return hashint(t, ivalue(key));
    case VS_TNUMFLT:
      return hashmod(t, l_hashfloat(fltvalue(key)));
    case VS_TSHRSTR:
      return hashstr(t, tsvalue(key));
    case VS_TLNGSTR:
      return hashpow2(t, vsS_hashlongstr(tsvalue(key)));
    case VS_TBOOLEAN:
      return hashboolean(t, bvalue(key));
    case VS_TLIGHTUSERDATA:
      return hashpointer(t, pvalue(key));
    case VS_TLCF:
      return hashpointer(t, fvalue(key));
    default:
      vs_assert(!ttisdeadkey(key));
      return hashpointer(t, gcvalue(key));
  }
}


/*
** returns the index for 'key' if 'key' is an appropriate key to live in
** the array part of the table, 0 otherwise.
*/
// ivalue(key)>0且不越界返回ivalue(key) 返回0代表无效
static unsigned int arrayindex (const TValue *key) {
  if (ttisinteger(key)) {
    vs_Integer k = ivalue(key);
    if (0 < k && (vs_Unsigned)k <= MAXASIZE)
      return cast(unsigned int, k);  /* 'key' is an appropriate array index */
  }
  return 0;  /* 'key' did not match some condition */
}


/*
** returns the index of a 'key' for table traversals. First goes all
** elements in the array part, then elements in the hash part. The
** beginning of a traversal is signaled by 0.
*/
// 查询表t中键key所在的位置 key是nil返回0
// 序号从1开始,哈希部分的第一个元素是sizearray+1
static unsigned int findindex (vs_State *L, Table *t, StkId key) {
  unsigned int i;
  if (ttisnil(key)) return 0;  /* first iteration */
  i = arrayindex(key);  // 计算在数组部分的位置 如果是0说明不在数组部分里
  if (i != 0 && i <= t->sizearray)  /* is 'key' inside array part? */
    // 在数组部分里 直接返回i
    return i;  /* yes; that's the index */
  else {  // 不在数组部分 在哈希部分 i=0
    int nx;
    Node *n = mainposition(t, key);  // 找到该key的的桶
    for (;;) {  /* check whether 'key' is somewhere in the chain 遍历这个桶 */
      /* key may be dead already, but it is ok to use it in 'next' */
      if (vsV_equalobj(gkey(n), key) ||
            (ttisdeadkey(gkey(n)) && iscollectable(key) &&
             deadvalue(gkey(n)) == gcvalue(key))) {
        i = cast_int(n - gnode(t, 0));  /* key index in hash table 得到查找结果与哈希部分开始位置的偏移 */
        /* hash elements are numbered after array ones 哈希部分的第一个元素序号是sizearray+1 */
        return (i + 1) + t->sizearray;  // 加上数组部分的偏移得到最终偏移
      }
      nx = gnext(n);  // 查询下一个元素与当前元素的偏移量
      if (nx == 0)
        vsG_runerror(L, "invalid key to 'next'");  /* key not found */
      else n += nx;
    }
  }
}


// 查询key对应的元素的下一项 实际就是pairs()函数的实现
// 修改栈上key为查询到的key,并将下一项的value放到栈上key的后面
// 返回1查询成功 0查询失败
// 首先查询数组部分key后面第一个不为nil的值
int vsH_next (vs_State *L, Table *t, StkId key) {
  unsigned int i = findindex(L, t, key);  /* find original element */
  for (; i < t->sizearray; i++) {  /* try first array part */
    if (!ttisnil(&t->array[i])) {  /* a non-nil value? */
      setivalue(key, i + 1); // 数组部分修改key直接设置数字就行 vs的数组从1开始 所以i位置代表i+1
      setobj2s(L, key+1, &t->array[i]);
      return 1;
    }
  }
  // 序号-sizearray就代表在哈希部分的位置
  for (i -= t->sizearray; cast_int(i) < sizenode(t); i++) {  /* hash part */
    // 此时i-1就是当前的值 i就是next值
    if (!ttisnil(gval(gnode(t, i)))) {  /* a non-nil value? */
      setobj2s(L, key, gkey(gnode(t, i)));  // 哈希部分修改key 因为类型不定需要重新拷贝
      setobj2s(L, key+1, gval(gnode(t, i)));
      return 1;
    }
  }
  return 0;  /* no more elements */
}


/*
** {=============================================================
** Rehash
** ==============================================================
*/

/*
** Compute the optimal size for the array part of table 't'. 'nums' is a
** "count array" where 'nums[i]' is the number of integers in the table
** between 2^(i - 1) + 1 and 2^i. 'pna' enters with the total number of
** integer keys in the table and leaves with the number of keys that
** will go to the array part; return the optimal size.
*/
// 计算表的数组部分的最优大小
// nums[i] 代表数组部分key为2^(i-1)和2^i之间存在的元素个数
// pna输入时代表表中整数key的个数 返回时代表数组部分中整数key的个数
// 返回值是数组部分的最优大小
static unsigned int computesizes (unsigned int nums[], unsigned int *pna) {
  int i;
  unsigned int twotoi;  /* 2^i (candidate for optimal size) */
  unsigned int a = 0;  /* number of elements smaller than 2^i 2^i内的元素个数 */
  unsigned int na = 0;  /* number of elements to go to array part 数组部分的元素个数 */
  unsigned int optimal = 0;  /* optimal size for array part 数组部分的最优大小 */
  /* loop while keys can fill more than half of total size */
  // 在1 2 4 8 ... 中寻找最优大小 必须满足该大小是能够找到的最大的填充超过一半位置的值
  for (i = 0, twotoi = 1; *pna > twotoi / 2; i++, twotoi *= 2) {
    if (nums[i] > 0) {
      a += nums[i];
      if (a > twotoi/2) {  /* more than half elements present? */
        optimal = twotoi;  /* optimal size (till now) */
        na = a;  /* all elements up to 'optimal' will go to array part */
      }
    }
  }
  vs_assert((optimal == 0 || optimal / 2 < na) && na <= optimal);
  *pna = na;
  return optimal;
}


// 用来求nums数组的辅助函数 输入key计算应该在nums数组的何处加1
// 返回1查询成功 0查询失败
static int countint (const TValue *key, unsigned int *nums) {
  unsigned int k = arrayindex(key);
  if (k != 0) {  /* is 'key' an appropriate array index? 找到了位置说明在数组部分 */
    nums[vsO_ceillog2(k)]++;  /* count as such nums的log2K位置加1 */
    return 1;
  }
  else
    return 0;
}


/*
** Count keys in array part of table 't': Fill 'nums[i]' with
** number of keys that will go into corresponding slice and return
** total number of non-nil keys.
*/
// 对表t计算nums数组 返回表t数组部分使用的元素个数
static unsigned int numusearray (const Table *t, unsigned int *nums) {
  int lg;
  unsigned int ttlg;  /* 2^lg */
  unsigned int ause = 0;  /* summation of 'nums' nums的和 数组部分总共的元素 */
  unsigned int i = 1;  /* count to traverse all array keys */
  /* traverse each slice */
  // 每一轮计算 2^(lg-1)和2^lg之间使用的元素个数
  for (lg = 0, ttlg = 1; lg <= MAXABITS; lg++, ttlg *= 2) {
    unsigned int lc = 0;  /* counter */
    unsigned int lim = ttlg;
    if (lim > t->sizearray) {
      lim = t->sizearray;  /* adjust upper limit */
      if (i > lim)
        break;  /* no more elements to count */
    }
    /* count elements in range (2^(lg - 1), 2^lg] */
    // 统计这一轮有多少元素使用
    for (; i <= lim; i++) {
      if (!ttisnil(&t->array[i-1]))
        lc++;
    }
    nums[lg] += lc;
    ause += lc;
  }
  return ause;
}


// 计算哈希部分有几个使用数字作为key的元素
// nums加上哈希部分数字key的影响
// pna执行结束加上哈希部分数字为key元素的个数
// 返回值是哈希部分总共被使用的元素个数
static int numusehash (const Table *t, unsigned int *nums, unsigned int *pna) {
  int totaluse = 0;  /* total number of elements 哈希部分总共使用的元素个数 */
  int ause = 0;  /* elements added to 'nums' (can go to array part) 哈希部分以数字为key的部分 */
  int i = sizenode(t);
  while (i--) {  // 遍历哈希部分
    Node *n = &t->node[i];
    if (!ttisnil(gval(n))) {
      ause += countint(gkey(n), nums);  // 数字key才能使ause加1
      totaluse++;  // 只要不为nil totaluse都有加1
    }
  }
  *pna += ause;
  return totaluse;
}


// 重设表t的数组部分的大小为size
// 不负责缩小空间的工作
static void setarrayvector (vs_State *L, Table *t, unsigned int size) {
  unsigned int i;
  vsM_reallocvector(L, t->array, t->sizearray, size, TValue);  // 重设array的大小
  for (i=t->sizearray; i<size; i++)  // 如果扩容 新增部分初始化为nil
     setnilvalue(&t->array[i]);
  t->sizearray = size;
}


// 为表t的哈希部分申请size个Node的空间
static void setnodevector (vs_State *L, Table *t, unsigned int size) {
  if (size == 0) {  /* no elements to hash part? 哈希部分大小是0 */
    // 设置初始值
    t->node = cast(Node *, dummynode);  /* use common 'dummynode' */
    t->lsizenode = 0;
    t->lastfree = NULL;  /* signal that it is using dummy node */
  }
  else {
    int i;
    int lsize = vsO_ceillog2(size);
    if (lsize > MAXHBITS)
      vsG_runerror(L, "table overflow");
    size = twoto(lsize);
    t->node = vsM_newvector(L, size, Node);  // 申请哈希部分空间
    for (i = 0; i < (int)size; i++) {  // 初始化申请出来的空间
      Node *n = gnode(t, i);
      gnext(n) = 0;
      setnilvalue(wgkey(n));  // key和value初始化为nil
      setnilvalue(gval(n));
    }
    t->lsizenode = cast_byte(lsize);
    // lastfree是申请空间最后的位置的下一个这里不能放值
    t->lastfree = gnode(t, size);  /* all positions are free */
  }
}


// 为表t重设数组部分大小为nasize,哈希部分大小为nhsize
void vsH_resize (vs_State *L, Table *t, unsigned int nasize,
                                          unsigned int nhsize) {
  unsigned int i;
  int j;
  // 保存更改大小前的数组部分大小和哈希部分大小
  unsigned int oldasize = t->sizearray;
  int oldhsize = allocsizenode(t);
  Node *nold = t->node;  /* save old hash ... 保存更改前哈希部分的指针 */
  if (nasize > oldasize)  /* array part must grow? 数组部分扩张 */
    setarrayvector(L, t, nasize);
  /* create new hash part with appropriate size 创建哈希部分的存放空间 */
  setnodevector(L, t, nhsize);  // 必须在缩小空间之前执行 因为缩小空间需要将值放进新的哈希部分
  if (nasize < oldasize) {  /* array part must shrink? 数组部分缩小空间 */
    t->sizearray = nasize;
    /* re-insert elements from vanishing slice */
    // 遍历即将消失的部分将他们重新放进表中的哈希部分
    for (i=nasize; i<oldasize; i++) {
      if (!ttisnil(&t->array[i]))
        vsH_setint(L, t, i + 1, &t->array[i]);  // 将原来的值放进哈希部分 重设t[i+1]
    }
    /* shrink array */
    vsM_reallocvector(L, t->array, oldasize, nasize, TValue);  // 缩小空间
  }
  /* re-insert elements from hash part */
  // 遍历原来的哈希部分 将原来的拷贝进新的哈希部分
  for (j = oldhsize - 1; j >= 0; j--) {
    Node *old = nold + j;
    if (!ttisnil(gval(old))) {
      /* doesn't need barrier/invalidate cache, as entry was
         already present in the table */
      // 在新哈希部分用vsH_set创建key 然后将value用setobj2t拷贝过来
      setobjt2t(L, vsH_set(L, t, gkey(old)), gval(old));  // 将旧位置的值拷贝过来
    }
  }
  if (oldhsize > 0)  /* not the dummy node? */
    vsM_freearray(L, nold, cast(size_t, oldhsize)); /* free old hash 释放掉旧哈希部分 */
}


// 只重新分配数组部分的空间
void vsH_resizearray (vs_State *L, Table *t, unsigned int nasize) {
  int nsize = allocsizenode(t);
  vsH_resize(L, t, nasize, nsize);
}

/*
** nums[i] = number of keys 'k' where 2^(i - 1) < k <= 2^i
*/
// 对表t即将增加额外的键ek 进行数组部分和哈希部分的空间重新分配
// nums[i]代表了 2^(i-1)和2^i之间存在的元素个数
// ek可能放在数组部分也可能在哈希部分 分配在数组部分可能引起数组部分的最优大小发生变化
// 例如当前t[1]=1 t[4]=4,此时1在数组部分,4在哈希部分
// 如果要设置t[2]=2,那么传入ek就是2
// 执行该函数将重新分配数组部分大小为4 哈希部分大小为0

static void rehash (vs_State *L, Table *t, const TValue *ek) {
  unsigned int asize;  /* optimal size for array part 数组部分的最优大小 */
  unsigned int na;  /* number of keys in the array part 数组部分的大小 */
  unsigned int nums[MAXABITS + 1];
  int i;
  int totaluse;
  for (i = 0; i <= MAXABITS; i++) nums[i] = 0;  /* reset counts */
  na = numusearray(t, nums);  /* count keys in array part 对nums处理数组部分 */
  totaluse = na;  /* all those keys are integer keys */
  totaluse += numusehash(t, nums, &na);  /* count keys in hash part 处理哈希部分 */
  /* count extra key 处理ek */
  na += countint(ek, nums);  // na可能加1或0 代表了ek可能放到数组部分也可能放到哈希部分
  totaluse++;
  /* compute new size for array part */
  asize = computesizes(nums, &na);  // 根据nums数组和当前数组部分的大小计算出来数组部分的最优大小
  /* resize the table to new computed sizes 重新分配数组和哈希部分的空间 */
  vsH_resize(L, t, asize, totaluse - na);
}



/*
** }=============================================================
*/


// 创建并初始化一个空表 数组和哈希部分大小都是0
Table *vsH_new (vs_State *L) {
  GCObject *o = vsC_newobj(L, VS_TTABLE, sizeof(Table));
  Table *t = gco2t(o);
  t->array = NULL;
  t->sizearray = 0;
  setnodevector(L, t, 0);
  return t;
}


// 释放一个表占用的空间
void vsH_free (vs_State *L, Table *t) {
  // 释放哈希部分
  if (!isdummy(t))
    vsM_freearray(L, t->node, cast(size_t, sizenode(t)));
  // 释放数组部分
  vsM_freearray(L, t->array, t->sizearray);
  // 释放表本身
  vsM_free(L, t);
}


// 从后往前找一个哈希部分的空闲位置并返回
// 让lastfree指向返回的位置
static Node *getfreepos (Table *t) {
  if (!isdummy(t)) {
    while (t->lastfree > t->node) {
      t->lastfree--;
      if (ttisnil(gkey(t->lastfree)))
        return t->lastfree;
    }
  }
  return NULL;  /* could not find a free place */
}



/*
** inserts a new key into a hash table; first, check whether key's main
** position is free. If not, check whether colliding node is in its main
** position or not: if it is not, move colliding node to an empty place and
** put new key in its main position; otherwise (colliding node is in its main
** position), new key goes to an empty position.
*/
// 在表t中新建一个key 返回该key对应的value部分
// 如果key是可转换成整数的浮点数 key就转换成整数
// 计算key应该在哪个哈希桶中 该桶为mp
// 如果桶的头不是nil 或者 哈希桶还没有空间
//   找一个哈希部分的空闲位置为f
//     f==NULL
//       则说明哈希部分空间不够在新增key的基础上重新分配数组和哈希部分空间
//       空间足够则调用vsH_set选择合适位置设置key
//   如果mp位置的值的主位置不是mp
//     调整碰撞位置所在链
//     移动这个值到f 新key设置到mp上
//   如果mp位置的值的主位置就是mp
//     将新key插入到链上 就在碰撞元素后面
//     设置mp=f 也就是新key设置到f上
// 设置新key到mp
TValue *vsH_newkey (vs_State *L, Table *t, const TValue *key) {
  Node *mp;  // mainposition主位置
  TValue aux;
  if (ttisnil(key)) vsG_runerror(L, "table index is nil");
  else if (ttisfloat(key)) {  // 可转换成整数的浮点按照整数对待
    vs_Integer k;
    if (vsV_tointeger(key, &k, 0)) {  /* does index fit in an integer? */
      setivalue(&aux, k);
      key = &aux;  /* insert it as an integer */
    }
    else if (vsi_numisnan(fltvalue(key)))
      vsG_runerror(L, "table index is NaN");
  }
  mp = mainposition(t, key);
  if (!ttisnil(gval(mp)) || isdummy(t)) {  /* main position is taken? */
    Node *othern;
    Node *f = getfreepos(t);  /* get a free place */
    if (f == NULL) {  /* cannot find a free place? 空间不足需要重新分配 */
      rehash(L, t, key);  /* grow table */
      /* whatever called 'newkey' takes care of TM cache */
      return vsH_set(L, t, key);  /* insert key into grown table */
    }
    vs_assert(!isdummy(t));
    othern = mainposition(t, gkey(mp));
    if (othern != mp) {  /* is colliding node out of its main position? 碰撞元素的主位置不在这里 */
      /* yes; move colliding node into free position 把碰撞元素移动到空闲位置 */
      // othern现在是碰撞元素所在链的头 从这个头开始往后查询一定能找到碰撞元素的上一个元素
      while (othern + gnext(othern) != mp)  /* find previous 找到碰撞元素的上一个元素 */
        othern += gnext(othern);
      // 现在othern指向碰撞元素的上一个元素 修改othern的next域
      gnext(othern) = cast_int(f - othern);  /* rechain to point to 'f' */
      *f = *mp;  /* copy colliding node into free pos. (mp->next also goes) 拷贝碰撞元素到新位置 */
      if (gnext(mp) != 0) {  // 碰撞元素与下一个元素的位置也重新连接起来
        gnext(f) += cast_int(mp - f);  /* correct 'next' */
        gnext(mp) = 0;  /* now 'mp' is free */
      }
      setnilvalue(gval(mp));
    }
    else {  /* colliding node is in its own main position 碰撞元素的主位置在这里 */
      /* new node will go into free position */
      // 将新元素放到一个空闲位置 然后与之前的位置串起来链
      // 新元素插到mp的后面
      if (gnext(mp) != 0)  // 新元素的下一个指向mp的下一个
        gnext(f) = cast_int((mp + gnext(mp)) - f);  /* chain new position */
      else vs_assert(gnext(f) == 0);
      gnext(mp) = cast_int(f - mp);  // mp的下一个指向新元素
      mp = f;
    }
  }
  // mp指向新节点
  setnodekey(L, &mp->i_key, key);  // 设置mp的key
  vsC_barrierback(L, t, key);
  vs_assert(ttisnil(gval(mp)));
  return gval(mp);  // 返回mp的value
}


/*
** search function for integers
*/
// 查询t[key] 先查询数组部分然后再查询哈希部分
// 返回查询到的结果 查询不到返回nil
const TValue *vsH_getint (Table *t, vs_Integer key) {
  /* (1 <= key && key <= t->sizearray) */
  if (l_castS2U(key) - 1 < t->sizearray)  // key在数组部分
    return &t->array[key - 1];
  else {
    Node *n = hashint(t, key);  // 找到应该在的哈希桶
    // 遍历哈希桶找到key一致的位置
    for (;;) {  /* check whether 'key' is somewhere in the chain */
      if (ttisinteger(gkey(n)) && ivalue(gkey(n)) == key)
        return gval(n);  /* that's it */
      else {
        int nx = gnext(n);
        if (nx == 0) break;
        n += nx;
      }
    }
    return vsO_nilobject;
  }
}


/*
** search function for short strings
*/
// 表t中查询短字符串
const TValue *vsH_getshortstr (Table *t, TString *key) {
  Node *n = hashstr(t, key);
  vs_assert(key->tt == VS_TSHRSTR);
  for (;;) {  /* check whether 'key' is somewhere in the chain */
    const TValue *k = gkey(n);
    if (ttisshrstring(k) && eqshrstr(tsvalue(k), key))
      return gval(n);  /* that's it */
    else {
      int nx = gnext(n);
      if (nx == 0)
        return vsO_nilobject;  /* not found */
      n += nx;
    }
  }
}


/*
** "Generic" get version. (Not that generic: not valid for integers,
** which may be in array part, nor for floats with integral values.)
*/
// 通用的get函数 用于从表t中查询key
static const TValue *getgeneric (Table *t, const TValue *key) {
  Node *n = mainposition(t, key);
  for (;;) {  /* check whether 'key' is somewhere in the chain */
    if (vsV_equalobj(gkey(n), key))
      return gval(n);  /* that's it */
    else {
      int nx = gnext(n);
      if (nx == 0)
        return vsO_nilobject;  /* not found */
      n += nx;
    }
  }
}


// 从表t中查询字符串类型的key
const TValue *vsH_getstr (Table *t, TString *key) {
  // 短字符串和长字符串相等的比较方法不同
  if (key->tt == VS_TSHRSTR)
    return vsH_getshortstr(t, key);
  else {  /* for long strings, use generic case */
    TValue ko;
    setsvalue(cast(vs_State *, NULL), &ko, key);
    return getgeneric(t, &ko);
  }
}


/*
** main search function
*/
// 主搜索函数 使用TValue作为key
const TValue *vsH_get (Table *t, const TValue *key) {
  switch (ttype(key)) {
    case VS_TSHRSTR: return vsH_getshortstr(t, tsvalue(key));
    case VS_TNUMINT: return vsH_getint(t, ivalue(key));
    case VS_TNIL: return vsO_nilobject;
    case VS_TNUMFLT: {
      vs_Integer k;
      if (vsV_tointeger(key, &k, 0)) /* index is int? */
        return vsH_getint(t, k);  /* use specialized version */
      /* else... */
    }  /* FALLTHROUGH */
    default:
      return getgeneric(t, key);
  }
}


/*
** beware: when using this function you probably need to check a GC
** barrier and invalidate the TM cache.
*/
// 查询t[key] 查询不到就新建这个key 返回t[key]节点的value部分的指针
TValue *vsH_set (vs_State *L, Table *t, const TValue *key) {
  const TValue *p = vsH_get(t, key);
  if (p != vsO_nilobject)
    return cast(TValue *, p);
  else return vsH_newkey(L, t, key);
}


// 对表t设置int的key
// t[key] = value
void vsH_setint (vs_State *L, Table *t, vs_Integer key, TValue *value) {
  const TValue *p = vsH_getint(t, key);
  TValue *cell;
  if (p != vsO_nilobject)
    cell = cast(TValue *, p);
  else {
    TValue k;
    setivalue(&k, key);
    cell = vsH_newkey(L, t, &k);
  }
  setobj2t(L, cell, value);
}


// 二分查找哈希部分
static int unbound_search (Table *t, unsigned int j) {
  unsigned int i = j;  /* i is zero or a present index */
  j++;
  /* find 'i' and 'j' such that i is present and j is not */
  while (!ttisnil(vsH_getint(t, j))) {
    i = j;
    if (j > cast(unsigned int, MAX_INT)/2) {  /* overflow? */
      /* table was built with bad purposes: resort to linear search */
      i = 1;
      while (!ttisnil(vsH_getint(t, i))) i++;
      return i - 1;
    }
    j *= 2;
  }
  /* now do a binary search between them */
  while (j - i > 1) {
    unsigned int m = (i+j)/2;
    if (ttisnil(vsH_getint(t, m))) j = m;
    else i = m;
  }
  return i;
}


/*
** Try to find a boundary in table 't'. A 'boundary' is an integer index
** such that t[i] is non-nil and t[i+1] is nil (and 0 if t[1] is nil).
*/
// 用于计算#t的函数
// 如果有数组部分计算数组部分 没有数组部分查询哈希部分
int vsH_getn (Table *t) {
  unsigned int j = t->sizearray;
  if (j > 0 && ttisnil(&t->array[j - 1])) {
    /* there is a boundary in the array part: (binary) search for it */
    unsigned int i = 0;
    while (j - i > 1) {
      unsigned int m = (i+j)/2;
      if (ttisnil(&t->array[m - 1])) j = m;
      else i = m;
    }
    return i;
  }
  /* else must find a boundary in hash part */
  else if (isdummy(t))  /* hash part is empty? */
    return j;  /* that is easy... */
  else return unbound_search(t, j);
}
