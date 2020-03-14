#ifndef vtable_h
#define vtable_h

#include "vobject.h"


#define gnode(t,i)	(&(t)->node[i])
#define gval(n)		(&(n)->i_val)
#define gnext(n)	((n)->i_key.nk.next)


/* 'const' to avoid wrong writings that can mess up field 'next' */
// 从Node对象种查询出key(TValue对象)
#define gkey(n)		cast(const TValue*, (&(n)->i_key.tvk))

/*
** writable version of 'gkey'; allows updates to individual fields,
** but not to the whole (which has incompatible type)
*/
// 可以对key的各个域单独进行写操作
#define wgkey(n)		(&(n)->i_key.nk)

/* true when 't' is using 'dummynode' as its hash part */
// 判断表t的哈希部分是不是dummynode
#define isdummy(t)		((t)->lastfree == NULL)


/* allocated size for hash nodes */
// 获取表t哈希部分实际分配的大小,也就是Node对象的个数
// 如果是dummy对象说明是0 其他情况再查询t->lsizenode
#define allocsizenode(t)	(isdummy(t) ? 0 : sizenode(t))


/* returns the key, given the value of a table entry */
// 通过Node对象中的i_val字段,查询i_key字段的地址
// offsetof用于计算结构体内某一字段与该结构体开头的偏移量
// 先计算i_val与Node的偏移量 得到Node对象地址 使用gkey获得i_key字段
#define keyfromval(v) \
  (gkey(cast(Node *, cast(char *, (v)) - offsetof(Node, i_val))))


VSI_FUNC const TValue *vsH_getint (Table *t, vs_Integer key);
VSI_FUNC void vsH_setint (vs_State *L, Table *t, vs_Integer key,
                                                    TValue *value);
VSI_FUNC const TValue *vsH_getshortstr (Table *t, TString *key);
VSI_FUNC const TValue *vsH_getstr (Table *t, TString *key);
VSI_FUNC const TValue *vsH_get (Table *t, const TValue *key);
VSI_FUNC TValue *vsH_newkey (vs_State *L, Table *t, const TValue *key);
VSI_FUNC TValue *vsH_set (vs_State *L, Table *t, const TValue *key);
VSI_FUNC Table *vsH_new (vs_State *L);
VSI_FUNC void vsH_resize (vs_State *L, Table *t, unsigned int nasize,
                                                    unsigned int nhsize);
VSI_FUNC void vsH_resizearray (vs_State *L, Table *t, unsigned int nasize);
VSI_FUNC void vsH_free (vs_State *L, Table *t);
VSI_FUNC int vsH_next (vs_State *L, Table *t, StkId key);
VSI_FUNC int vsH_getn (Table *t);


#endif
