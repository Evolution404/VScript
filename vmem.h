#ifndef vmem_h
#define vmem_h


#include <stddef.h>

#include "vlimits.h"
#include "vs.h"


// 这个宏重新分配数组b的内存 从on个元素到n个元素 每个元素的大小是e
// (n+1) > MAX_SIZET/e 目的是确保n*e不会溢出
#define vsM_reallocv(L,b,on,n,e) \
  (((sizeof(n) >= sizeof(size_t) && cast(size_t, (n)) + 1 > MAX_SIZET/(e)) \
      ? vsM_toobig(L) : cast_void(0)) , \
   vsM_realloc_(L, (b), (on)*(e), (n)*(e)))

/*
** Arrays of chars do not need any test
*/
// 分配字符串数组
#define vsM_reallocvchar(L,b,on,n)  \
    cast(char *, vsM_realloc_(L, (b), (on)*sizeof(char), (n)*sizeof(char)))

// 释放内存 指定块的大小 不需要传nsize了 一定是0
#define vsM_freemem(L, b, s)	vsM_realloc_(L, (b), (s), 0)
// 释放内存 不指定块的大小 释放的大小就是块的结构体的大小
#define vsM_free(L, b)		vsM_realloc_(L, (b), sizeof(*(b)), 0)
// 释放数组的内存 传入指针和元素的个数
#define vsM_freearray(L, b, n)   vsM_realloc_(L, (b), (n)*sizeof(*(b)), 0)

// 分配s大小的内存
#define vsM_malloc(L,s)	vsM_realloc_(L, NULL, 0, (s))
// 分配t类型大小的内存 并进行类型转换
#define vsM_new(L,t)		cast(t *, vsM_malloc(L, sizeof(t)))
// 分配t类型n个元素数组
#define vsM_newvector(L,n,t) \
		cast(t *, vsM_reallocv(L, NULL, 0, n, sizeof(t)))

// 直接分配s大小内存 tag没有实际意义只是为了调用处的可读性
#define vsM_newobject(L,tag,s)	vsM_realloc_(L, NULL, tag, (s))

// 在数组已经放满的情况下增长数组的大小 一般是进行翻倍,调用后数组大小最小为4
// v要增长的数组 nelems数组中当前元素的个数 size数组当前的大小
// t数组元素类型 limit数组大小上限 e用于错误信息
#define vsM_growvector(L,v,nelems,size,t,limit,e) \
          if ((nelems)+1 > (size)) \
            ((v)=cast(t *, vsM_growaux_(L,v,&(size),sizeof(t),limit,e)))

// 重新分配数组大小并进行类型转换
// v 要重新分配的数组 oldn分配前元素个数 n要分配的元素个数 t元素的类型
#define vsM_reallocvector(L, v,oldn,n,t) \
   ((v)=cast(t *, vsM_reallocv(L, v, oldn, n, sizeof(t))))

VSI_FUNC l_noret vsM_toobig (vs_State *L);

/* not to be called directly */
VSI_FUNC void *vsM_realloc_ (vs_State *L, void *block, size_t oldsize,
                                                          size_t size);
VSI_FUNC void *vsM_growaux_ (vs_State *L, void *block, int *size,
                               size_t size_elem, int limit,
                               const char *what);

#endif

