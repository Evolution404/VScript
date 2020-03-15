/*
** $Id: ltablib.c,v 1.93 2016/02/25 19:41:54 roberto Exp $
** Library for Table Manipulation
** See Copyright Notice in vs.h
*/

#define ltablib_c

#include <limits.h>
#include <stddef.h>
#include <string.h>

#include "vs.h"

#include "vauxlib.h"
#include "vslib.h"


#define aux_getn(L,n)	(checktab(L, n), vsL_len(L, n))



/*
** Check that 'arg' either is a table or can behave like one (that is,
** has a metatable with the required metamethods)
*/
static void checktab (vs_State *L, int arg) {
  if (vs_type(L, arg) != VS_TTABLE) {  /* is it not a table? */
    vsL_checktype(L, arg, VS_TTABLE);  /* force an error */
  }
}


static int tinsert (vs_State *L) {
  vs_Integer e = aux_getn(L, 1) + 1;  /* first empty element */
  vs_Integer pos;  /* where to insert new element */
  switch (vs_gettop(L)) {
    case 2: {  /* called with only 2 arguments */
      pos = e;  /* insert new element at the end */
      break;
    }
    case 3: {
      vs_Integer i;
      pos = vsL_checkinteger(L, 2);  /* 2nd argument is the position */
      vsL_argcheck(L, 1 <= pos && pos <= e, 2, "position out of bounds");
      for (i = e; i > pos; i--) {  /* move up elements */
        vs_geti(L, 1, i - 1);
        vs_seti(L, 1, i);  /* t[i] = t[i - 1] */
      }
      break;
    }
    default: {
      return vsL_error(L, "wrong number of arguments to 'insert'");
    }
  }
  vs_seti(L, 1, pos);  /* t[pos] = v */
  return 0;
}


static int tremove (vs_State *L) {
  vs_Integer size = aux_getn(L, 1);
  vs_Integer pos = vsL_optinteger(L, 2, size);
  if (pos != size)  /* validate 'pos' if given */
    vsL_argcheck(L, 1 <= pos && pos <= size + 1, 1, "position out of bounds");
  vs_geti(L, 1, pos);  /* result = t[pos] */
  for ( ; pos < size; pos++) {
    vs_geti(L, 1, pos + 1);
    vs_seti(L, 1, pos);  /* t[pos] = t[pos + 1] */
  }
  vs_pushnil(L);
  vs_seti(L, 1, pos);  /* t[pos] = nil */
  return 1;
}


/*
** Copy elements (1[f], ..., 1[e]) into (tt[t], tt[t+1], ...). Whenever
** possible, copy in increasing order, which is better for rehashing.
** "possible" means destination after original range, or smaller
** than origin, or copying to another table.
*/
static int tmove (vs_State *L) {
  vs_Integer f = vsL_checkinteger(L, 2);
  vs_Integer e = vsL_checkinteger(L, 3);
  vs_Integer t = vsL_checkinteger(L, 4);
  int tt = !vs_isnoneornil(L, 5) ? 5 : 1;  /* destination table */
  checktab(L, 1);
  checktab(L, tt);
  if (e >= f) {  /* otherwise, nothing to move */
    vs_Integer n, i;
    vsL_argcheck(L, f > 0 || e < VS_MAXINTEGER + f, 3,
                  "too many elements to move");
    n = e - f + 1;  /* number of elements to move */
    vsL_argcheck(L, t <= VS_MAXINTEGER - n + 1, 4,
                  "destination wrap around");
    if (t > e || t <= f || (tt != 1 && !vs_compare(L, 1, tt, VS_OPEQ))) {
      for (i = 0; i < n; i++) {
        vs_geti(L, 1, f + i);
        vs_seti(L, tt, t + i);
      }
    }
    else {
      for (i = n - 1; i >= 0; i--) {
        vs_geti(L, 1, f + i);
        vs_seti(L, tt, t + i);
      }
    }
  }
  vs_pushvalue(L, tt);  /* return destination table */
  return 1;
}


static void addfield (vs_State *L, vsL_Buffer *b, vs_Integer i) {
  vs_geti(L, 1, i);
  if (!vs_isstring(L, -1))
    vsL_error(L, "invalid value (%s) at index %d in table for 'concat'",
                  vsL_typename(L, -1), i);
  vsL_addvalue(b);
}


static int tconcat (vs_State *L) {
  vsL_Buffer b;
  vs_Integer last = aux_getn(L, 1);
  size_t lsep;
  const char *sep = vsL_optlstring(L, 2, "", &lsep);
  vs_Integer i = vsL_optinteger(L, 3, 1);
  last = vsL_optinteger(L, 4, last);
  vsL_buffinit(L, &b);
  for (; i < last; i++) {
    addfield(L, &b, i);
    vsL_addlstring(&b, sep, lsep);
  }
  if (i == last)  /* add last value (if interval was not empty) */
    addfield(L, &b, i);
  vsL_pushresult(&b);
  return 1;
}


/*
** {======================================================
** Pack/unpack
** =======================================================
*/

static int pack (vs_State *L) {
  int i;
  int n = vs_gettop(L);  /* number of elements to pack */
  vs_createtable(L, n, 1);  /* create result table */
  vs_insert(L, 1);  /* put it at index 1 */
  for (i = n; i >= 1; i--)  /* assign elements */
    vs_seti(L, 1, i);
  vs_pushinteger(L, n);
  vs_setfield(L, 1, "n");  /* t.n = number of elements */
  return 1;  /* return table */
}


static int unpack (vs_State *L) {
  vs_Unsigned n;
  vs_Integer i = vsL_optinteger(L, 2, 1);
  vs_Integer e = vsL_opt(L, vsL_checkinteger, 3, vsL_len(L, 1));
  if (i > e) return 0;  /* empty range */
  n = (vs_Unsigned)e - i;  /* number of elements minus 1 (avoid overflows) */
  if (n >= (unsigned int)INT_MAX  || !vs_checkstack(L, (int)(++n)))
    return vsL_error(L, "too many results to unpack");
  for (; i < e; i++) {  /* push arg[i..e - 1] (to avoid overflows) */
    vs_geti(L, 1, i);
  }
  vs_geti(L, 1, e);  /* push last element */
  return (int)n;
}

/* }====================================================== */



/*
** {======================================================
** Quicksort
** (based on 'Algorithms in MODULA-3', Robert Sedgewick;
**  Addison-Wesley, 1993.)
** =======================================================
*/


/* type for array indices */
typedef unsigned int IdxT;


/*
** Produce a "random" 'unsigned int' to randomize pivot choice. This
** macro is used only when 'sort' detects a big imbalance in the result
** of a partition. (If you don't want/need this "randomness", ~0 is a
** good choice.)
*/
#if !defined(l_randomizePivot)		/* { */

#include <time.h>

/* size of 'e' measured in number of 'unsigned int's */
#define sof(e)		(sizeof(e) / sizeof(unsigned int))

/*
** Use 'time' and 'clock' as sources of "randomness". Because we don't
** know the types 'clock_t' and 'time_t', we cannot cast them to
** anything without risking overflows. A safe way to use their values
** is to copy them to an array of a known type and use the array values.
*/
static unsigned int l_randomizePivot (void) {
  clock_t c = clock();
  time_t t = time(NULL);
  unsigned int buff[sof(c) + sof(t)];
  unsigned int i, rnd = 0;
  memcpy(buff, &c, sof(c) * sizeof(unsigned int));
  memcpy(buff + sof(c), &t, sof(t) * sizeof(unsigned int));
  for (i = 0; i < sof(buff); i++)
    rnd += buff[i];
  return rnd;
}

#endif					/* } */


/* arrays larger than 'RANLIMIT' may use randomized pivots */
#define RANLIMIT	100u


static void set2 (vs_State *L, IdxT i, IdxT j) {
  vs_seti(L, 1, i);
  vs_seti(L, 1, j);
}


/*
** Return true iff value at stack index 'a' is less than the value at
** index 'b' (according to the order of the sort).
*/
static int sort_comp (vs_State *L, int a, int b) {
  if (vs_isnil(L, 2))  /* no function? */
    return vs_compare(L, a, b, VS_OPLT);  /* a < b */
  else {  /* function */
    int res;
    vs_pushvalue(L, 2);    /* push function */
    vs_pushvalue(L, a-1);  /* -1 to compensate function */
    vs_pushvalue(L, b-2);  /* -2 to compensate function and 'a' */
    vs_call(L, 2, 1);      /* call function */
    res = vs_toboolean(L, -1);  /* get result */
    vs_pop(L, 1);          /* pop result */
    return res;
  }
}


/*
** Does the partition: Pivot P is at the top of the stack.
** precondition: a[lo] <= P == a[up-1] <= a[up],
** so it only needs to do the partition from lo + 1 to up - 2.
** Pos-condition: a[lo .. i - 1] <= a[i] == P <= a[i + 1 .. up]
** returns 'i'.
*/
static IdxT partition (vs_State *L, IdxT lo, IdxT up) {
  IdxT i = lo;  /* will be incremented before first use */
  IdxT j = up - 1;  /* will be decremented before first use */
  /* loop invariant: a[lo .. i] <= P <= a[j .. up] */
  for (;;) {
    /* next loop: repeat ++i while a[i] < P */
    while (vs_geti(L, 1, ++i), sort_comp(L, -1, -2)) {
      if (i == up - 1)  /* a[i] < P  but a[up - 1] == P  ?? */
        vsL_error(L, "invalid order function for sorting");
      vs_pop(L, 1);  /* remove a[i] */
    }
    /* after the loop, a[i] >= P and a[lo .. i - 1] < P */
    /* next loop: repeat --j while P < a[j] */
    while (vs_geti(L, 1, --j), sort_comp(L, -3, -1)) {
      if (j < i)  /* j < i  but  a[j] > P ?? */
        vsL_error(L, "invalid order function for sorting");
      vs_pop(L, 1);  /* remove a[j] */
    }
    /* after the loop, a[j] <= P and a[j + 1 .. up] >= P */
    if (j < i) {  /* no elements out of place? */
      /* a[lo .. i - 1] <= P <= a[j + 1 .. i .. up] */
      vs_pop(L, 1);  /* pop a[j] */
      /* swap pivot (a[up - 1]) with a[i] to satisfy pos-condition */
      set2(L, up - 1, i);
      return i;
    }
    /* otherwise, swap a[i] - a[j] to restore invariant and repeat */
    set2(L, i, j);
  }
}


/*
** Choose an element in the middle (2nd-3th quarters) of [lo,up]
** "randomized" by 'rnd'
*/
static IdxT choosePivot (IdxT lo, IdxT up, unsigned int rnd) {
  IdxT r4 = (up - lo) / 4;  /* range/4 */
  IdxT p = rnd % (r4 * 2) + (lo + r4);
  vs_assert(lo + r4 <= p && p <= up - r4);
  return p;
}


/*
** QuickSort algorithm (recursive function)
*/
static void auxsort (vs_State *L, IdxT lo, IdxT up,
                                   unsigned int rnd) {
  while (lo < up) {  /* loop for tail recursion */
    IdxT p;  /* Pivot index */
    IdxT n;  /* to be used later */
    /* sort elements 'lo', 'p', and 'up' */
    vs_geti(L, 1, lo);
    vs_geti(L, 1, up);
    if (sort_comp(L, -1, -2))  /* a[up] < a[lo]? */
      set2(L, lo, up);  /* swap a[lo] - a[up] */
    else
      vs_pop(L, 2);  /* remove both values */
    if (up - lo == 1)  /* only 2 elements? */
      return;  /* already sorted */
    if (up - lo < RANLIMIT || rnd == 0)  /* small interval or no randomize? */
      p = (lo + up)/2;  /* middle element is a good pivot */
    else  /* for larger intervals, it is worth a random pivot */
      p = choosePivot(lo, up, rnd);
    vs_geti(L, 1, p);
    vs_geti(L, 1, lo);
    if (sort_comp(L, -2, -1))  /* a[p] < a[lo]? */
      set2(L, p, lo);  /* swap a[p] - a[lo] */
    else {
      vs_pop(L, 1);  /* remove a[lo] */
      vs_geti(L, 1, up);
      if (sort_comp(L, -1, -2))  /* a[up] < a[p]? */
        set2(L, p, up);  /* swap a[up] - a[p] */
      else
        vs_pop(L, 2);
    }
    if (up - lo == 2)  /* only 3 elements? */
      return;  /* already sorted */
    vs_geti(L, 1, p);  /* get middle element (Pivot) */
    vs_pushvalue(L, -1);  /* push Pivot */
    vs_geti(L, 1, up - 1);  /* push a[up - 1] */
    set2(L, p, up - 1);  /* swap Pivot (a[p]) with a[up - 1] */
    p = partition(L, lo, up);
    /* a[lo .. p - 1] <= a[p] == P <= a[p + 1 .. up] */
    if (p - lo < up - p) {  /* lower interval is smaller? */
      auxsort(L, lo, p - 1, rnd);  /* call recursively for lower interval */
      n = p - lo;  /* size of smaller interval */
      lo = p + 1;  /* tail call for [p + 1 .. up] (upper interval) */
    }
    else {
      auxsort(L, p + 1, up, rnd);  /* call recursively for upper interval */
      n = up - p;  /* size of smaller interval */
      up = p - 1;  /* tail call for [lo .. p - 1]  (lower interval) */
    }
    if ((up - lo) / 128 > n) /* partition too imbalanced? */
      rnd = l_randomizePivot();  /* try a new randomization */
  }  /* tail call auxsort(L, lo, up, rnd) */
}


static int sort (vs_State *L) {
  vs_Integer n = aux_getn(L, 1);
  if (n > 1) {  /* non-trivial interval? */
    vsL_argcheck(L, n < INT_MAX, 1, "array too big");
    if (!vs_isnoneornil(L, 2))  /* is there a 2nd argument? */
      vsL_checktype(L, 2, VS_TFUNCTION);  /* must be a function */
    vs_settop(L, 2);  /* make sure there are two arguments */
    auxsort(L, 1, (IdxT)n, 0);
  }
  return 0;
}

/* }====================================================== */


static const vsL_Reg tab_funcs[] = {
  {"concat", tconcat},
  {"insert", tinsert},
  {"pack", pack},
  {"unpack", unpack},
  {"remove", tremove},
  {"move", tmove},
  {"sort", sort},
  {NULL, NULL}
};


VSMOD_API int vsopen_table (vs_State *L) {
  vsL_newlib(L, tab_funcs);
  return 1;
}

