/*
** $Id: ltablib.c $
** Library for Table Manipulation
** See Copyright Notice in lua.h
*/

#define ltablib_c
#define LUA_LIB

#include <stdlib.h>

#include "lprefix.h"


#include <limits.h>
#include <stddef.h>
#include <string.h>

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"

// 方便. 最好不要这么做。
#include "lapi.h"
#include "ltable.h"
#include "lvm.h"
#include "lgc.h"

/*
** Operations that an object must define to mimic a table
** (some functions only need some of them)
*/
#define TAB_R	1			/* read */
#define TAB_W	2			/* write */
#define TAB_L	4			/* length */
#define TAB_RW	(TAB_R | TAB_W)		/* read/write */


#define aux_getn(L,n,w)	(checktab(L, n, (w) | TAB_L), luaL_len(L, n))


static int checkfield (lua_State *L, const char *key, int n) {
  lua_pushstring(L, key);
  return (lua_rawget(L, -n) != LUA_TNIL);
}


/*
** Check that 'arg' either is a table or can behave like one (that is,
** has a metatable with the required metamethods)
*/
static void checktab (lua_State *L, int arg, int what) {
  if (lua_type(L, arg) != LUA_TTABLE) {  /* is it not a table? */
    int n = 1;  /* number of elements to pop */
    if (lua_getmetatable(L, arg) &&  /* must have metatable */
        (!(what & TAB_R) || checkfield(L, "__index", ++n)) &&
        (!(what & TAB_W) || checkfield(L, "__newindex", ++n)) &&
        (!(what & TAB_L) || checkfield(L, "__len", ++n))) {
      lua_pop(L, n);  /* pop metatable and tested metamethods */
    }
    else
      luaL_checktype(L, arg, LUA_TTABLE);  /* force an error */
  }
}


static int tinsert (lua_State *L) {
  lua_Integer pos;  /* where to insert new element */
  lua_Integer e = aux_getn(L, 1, TAB_RW);
  e = luaL_intop(+, e, 1);  /* first empty element */
  switch (lua_gettop(L)) {
    case 2: {  /* called with only 2 arguments */
      pos = e;  /* insert new element at the end */
      break;
    }
    case 3: {
      lua_Integer i;
      pos = luaL_checkinteger(L, 2);  /* 2nd argument is the position */
      /* check whether 'pos' is in [1, e] */
      luaL_argcheck(L, (lua_Unsigned)pos - 1u < (lua_Unsigned)e, 2,
                       "position out of bounds");
      for (i = e; i > pos; i--) {  /* move up elements */
        lua_geti(L, 1, i - 1);
        lua_seti(L, 1, i);  /* t[i] = t[i - 1] */
      }
      break;
    }
    default: {
      return luaL_error(L, "wrong number of arguments to 'insert'");
    }
  }
  lua_seti(L, 1, pos);  /* t[pos] = v */
  return 0;
}


static int tremove (lua_State *L) {
  lua_Integer size = aux_getn(L, 1, TAB_RW);
  lua_Integer pos = luaL_optinteger(L, 2, size);
  if (pos != size)  /* validate 'pos' if given */
    /* check whether 'pos' is in [1, size + 1] */
    luaL_argcheck(L, (lua_Unsigned)pos - 1u <= (lua_Unsigned)size, 1,
                     "position out of bounds");
  lua_geti(L, 1, pos);  /* result = t[pos] */
  for ( ; pos < size; pos++) {
    lua_geti(L, 1, pos + 1);
    lua_seti(L, 1, pos);  /* t[pos] = t[pos + 1] */
  }
  lua_pushnil(L);
  lua_seti(L, 1, pos);  /* remove entry t[pos] */
  return 1;
}


/*
** Copy elements (1[f], ..., 1[e]) into (tt[t], tt[t+1], ...). Whenever
** possible, copy in increasing order, which is better for rehashing.
** "possible" means destination after original range, or smaller
** than origin, or copying to another table.
*/
static int tmove (lua_State *L) {
  lua_Integer f = luaL_checkinteger(L, 2);
  lua_Integer e = luaL_checkinteger(L, 3);
  lua_Integer t = luaL_checkinteger(L, 4);
  int tt = !lua_isnoneornil(L, 5) ? 5 : 1;  /* destination table */
  checktab(L, 1, TAB_R);
  checktab(L, tt, TAB_W);
  if (e >= f) {  /* otherwise, nothing to move */
    lua_Integer n, i;
    luaL_argcheck(L, f > 0 || e < LUA_MAXINTEGER + f, 3,
                  "too many elements to move");
    n = e - f + 1;  /* number of elements to move */
    luaL_argcheck(L, t <= LUA_MAXINTEGER - n + 1, 4,
                  "destination wrap around");
    if (t > e || t <= f || (tt != 1 && !lua_compare(L, 1, tt, LUA_OPEQ))) {
      for (i = 0; i < n; i++) {
        lua_geti(L, 1, f + i);
        lua_seti(L, tt, t + i);
      }
    }
    else {
      for (i = n - 1; i >= 0; i--) {
        lua_geti(L, 1, f + i);
        lua_seti(L, tt, t + i);
      }
    }
  }
  lua_pushvalue(L, tt);  /* return destination table */
  return 1;
}


static void addfield (lua_State *L, luaL_Buffer *b, lua_Integer i) {
  lua_geti(L, 1, i);
  if (l_unlikely(!lua_isstring(L, -1)))
    luaL_error(L, "invalid value (%s) at index %I in table for 'concat'",
                  luaL_typename(L, -1), (LUAI_UACINT)i);
  luaL_addvalue(b);
}


static int tconcat (lua_State *L) {
  luaL_Buffer b;
  lua_Integer last = aux_getn(L, 1, TAB_R);
  size_t lsep;
  const char *sep = luaL_optlstring(L, 2, "", &lsep);
  lua_Integer i = luaL_optinteger(L, 3, 1);
  last = luaL_optinteger(L, 4, last);
  luaL_buffinit(L, &b);
  for (; i < last; i++) {
    addfield(L, &b, i);
    luaL_addlstring(&b, sep, lsep);
  }
  if (i == last)  /* add last value (if interval was not empty) */
    addfield(L, &b, i);
  luaL_pushresult(&b);
  return 1;
}


/*
** {======================================================
** Pack/unpack
** =======================================================
*/

static int tpack (lua_State *L) {
  int i;
  int n = lua_gettop(L);  /* number of elements to pack */
  // mod@om table.pack 也适合用 array
  lua_createarray(L, n);
  // lua_createtable(L, n, 0);  /* create result table */
  lua_insert(L, 1);  /* put it at index 1 */
  for (i = n; i >= 1; i--)  /* assign elements */
    lua_seti(L, 1, i);
  // compat@om 不再支持这个了，这个地方就不兼容了
  // lua_pushinteger(L, n);
  // lua_setfield(L, 1, "n");  /* t.n = number of elements */ 
  return 1;  /* return table */
}


static int tunpack (lua_State *L) {
  lua_Unsigned n;
  lua_Integer i = luaL_optinteger(L, 2, 1);
  lua_Integer e = luaL_opt(L, luaL_checkinteger, 3, luaL_len(L, 1));
  if (i > e) return 0;  /* empty range */
  n = (lua_Unsigned)e - i;  /* number of elements minus 1 (avoid overflows) */
  if (l_unlikely(n >= (unsigned int)INT_MAX  ||
                 !lua_checkstack(L, (int)(++n))))
    return luaL_error(L, "too many results to unpack");
  for (; i < e; i++) {  /* push arg[i..e - 1] (to avoid overflows) */
    lua_geti(L, 1, i);
  }
  lua_geti(L, 1, e);  /* push last element */
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


static void set2 (lua_State *L, IdxT i, IdxT j) {
  lua_seti(L, 1, i);
  lua_seti(L, 1, j);
}


/*
** Return true iff value at stack index 'a' is less than the value at
** index 'b' (according to the order of the sort).
*/
static int sort_comp (lua_State *L, int a, int b) {
  if (lua_isnil(L, 2))  /* no function? */
    return lua_compare(L, a, b, LUA_OPLT);  /* a < b */
  else {  /* function */
    int res;
    lua_pushvalue(L, 2);    /* push function */
    lua_pushvalue(L, a-1);  /* -1 to compensate function */
    lua_pushvalue(L, b-2);  /* -2 to compensate function and 'a' */
    lua_call(L, 2, 1);      /* call function */
    res = lua_toboolean(L, -1);  /* get result */
    lua_pop(L, 1);          /* pop result */
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
static IdxT partition (lua_State *L, IdxT lo, IdxT up) {
  IdxT i = lo;  /* will be incremented before first use */
  IdxT j = up - 1;  /* will be decremented before first use */
  /* loop invariant: a[lo .. i] <= P <= a[j .. up] */
  for (;;) {
    /* next loop: repeat ++i while a[i] < P */
    while ((void)lua_geti(L, 1, ++i), sort_comp(L, -1, -2)) {
      if (l_unlikely(i == up - 1))  /* a[i] < P  but a[up - 1] == P  ?? */
        luaL_error(L, "invalid order function for sorting");
      lua_pop(L, 1);  /* remove a[i] */
    }
    /* after the loop, a[i] >= P and a[lo .. i - 1] < P */
    /* next loop: repeat --j while P < a[j] */
    while ((void)lua_geti(L, 1, --j), sort_comp(L, -3, -1)) {
      if (l_unlikely(j < i))  /* j < i  but  a[j] > P ?? */
        luaL_error(L, "invalid order function for sorting");
      lua_pop(L, 1);  /* remove a[j] */
    }
    /* after the loop, a[j] <= P and a[j + 1 .. up] >= P */
    if (j < i) {  /* no elements out of place? */
      /* a[lo .. i - 1] <= P <= a[j + 1 .. i .. up] */
      lua_pop(L, 1);  /* pop a[j] */
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
  lua_assert(lo + r4 <= p && p <= up - r4);
  return p;
}


/*
** Quicksort algorithm (recursive function)
*/
static void auxsort (lua_State *L, IdxT lo, IdxT up,
                                   unsigned int rnd) {
  while (lo < up) {  /* loop for tail recursion */
    IdxT p;  /* Pivot index */
    IdxT n;  /* to be used later */
    /* sort elements 'lo', 'p', and 'up' */
    lua_geti(L, 1, lo);
    lua_geti(L, 1, up);
    if (sort_comp(L, -1, -2))  /* a[up] < a[lo]? */
      set2(L, lo, up);  /* swap a[lo] - a[up] */
    else
      lua_pop(L, 2);  /* remove both values */
    if (up - lo == 1)  /* only 2 elements? */
      return;  /* already sorted */
    if (up - lo < RANLIMIT || rnd == 0)  /* small interval or no randomize? */
      p = (lo + up)/2;  /* middle element is a good pivot */
    else  /* for larger intervals, it is worth a random pivot */
      p = choosePivot(lo, up, rnd);
    lua_geti(L, 1, p);
    lua_geti(L, 1, lo);
    if (sort_comp(L, -2, -1))  /* a[p] < a[lo]? */
      set2(L, p, lo);  /* swap a[p] - a[lo] */
    else {
      lua_pop(L, 1);  /* remove a[lo] */
      lua_geti(L, 1, up);
      if (sort_comp(L, -1, -2))  /* a[up] < a[p]? */
        set2(L, p, up);  /* swap a[up] - a[p] */
      else
        lua_pop(L, 2);
    }
    if (up - lo == 2)  /* only 3 elements? */
      return;  /* already sorted */
    lua_geti(L, 1, p);  /* get middle element (Pivot) */
    lua_pushvalue(L, -1);  /* push Pivot */
    lua_geti(L, 1, up - 1);  /* push a[up - 1] */
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


static int sort (lua_State *L) {
  lua_Integer n = aux_getn(L, 1, TAB_RW);
  if (n > 1) {  /* non-trivial interval? */
    luaL_argcheck(L, n < INT_MAX, 1, "array too big");
    if (!lua_isnoneornil(L, 2))  /* is there a 2nd argument? */
      luaL_checktype(L, 2, LUA_TFUNCTION);  /* must be a function */
    lua_settop(L, 2);  /* make sure there are two arguments */
    auxsort(L, 1, (IdxT)n, 0);
  }
  return 0;
}

/* }====================================================== */

static int newmap (lua_State *L) {
  int n = (int)luaL_optinteger(L, 1, 0);
  lua_createtable(L, 0, n);
  return 1;  /* return table */
}

static int newarray(lua_State *L) {
  int n = (int)luaL_optinteger(L, 1, 0);
  lua_createarray(L, n);
  return 1;  /* return table */
}

static int get_capacity(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  const TValue *o = luaA_index2value(L, 1);
  Table* t = hvalue(o);
  lua_pushinteger(L, t->data ? sizenode(t) : 0);
  return 1;  /* return table */
}

// push next_idx,key,value or nothing
static int tablib_next(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  int32_t n = (int32_t)luaL_optinteger(L, 2, 0);
  if(n < 0) {
    return 0;
  }
  const TValue *o = luaA_index2value(L, 1);
  Table* t = hvalue(o);
  int next_idx = luaH_itor_next(L, t, n, L->top + 1);
  if (next_idx > 0) {
    setivalue(s2v(L->top), next_idx);
    api_incr_top_n(L, 3);
    return 3;
  }
  return 0;
}

typedef struct FStableSortContext
{
  lua_State *L;
  Table *t;
  int func_idx;// 比较函数的索引。!=0 时有效。同时也是getid函数的索引。
  int is_reverse;// 反向排序-1,否则1
}FStableSortContext;

/*
 之所有这么定义，时为了排完序后修改原来的table方便。【缺点：现在排序，差不多要临时获取和原来一样大的内存了】
*/
typedef struct FSSortIdxNode
{
  TValue id;// getid 函数返回的结果
  int32_t idx;// 数组索引 或者 map 内部索引。同时用作稳定排序的依据。start from 0
}FSSortIdxNode;

typedef union FSSortMapNode
{
  Node t_data;
  FSSortIdxNode idata;
}FSSortMapNode;

typedef union FSSortArrayNode
{
  TValue t_data;
  FSSortIdxNode idata; // 如果时32位，sizeof(FSSortArrayNode) > sizeof(TValue) 会浪费内存
}FSSortArrayNode;



// 传入比较函数。不判断nil。要交由用户函数自己处理
static int stable_sort_cmp_func_call (FStableSortContext *co , TValue *a, TValue *b) {
  if (co->func_idx != 0) {
    lua_State *L = co->L;
    lua_pushvalue(L, co->func_idx);
    luaA_pushvalue(L, a);
    luaA_pushvalue(L, b);
    lua_call(L, 2, 1);
    int n = (int)luaL_checkinteger(L, -1);
    return n;
  }
  else {
    return luaV_cmpobj_safe(a, b);
  }
}


#define stable_sort_check_idx(L,t,idx) {if l_unlikely((idx) >= (t)->count) { \
    luaL_error(L, "table count shrink when sort, maybe sort recursive in cmp func");\
    return 0;/* just avoid warning */ \
  } }

// 完蛋。msvc 不兼容过的样子
// typedef int (*FStableSortCmpFunc)(const void* a, const void* b, void *c);

#define qsort_cmp_func_define(name) static int name(void *c, const void* a, const void* b) 
typedef int (*FStableSortCmpFunc)(void *c, const void* a, const void* b);


qsort_cmp_func_define(stable_sort_cmp_map_value_up){
  FSSortMapNode *na = (FSSortMapNode*)a;
  FSSortMapNode *nb = (FSSortMapNode*)b;
  int32_t idx_a = na->idata.idx;
  int32_t idx_b = nb->idata.idx;
  FStableSortContext *co = (FStableSortContext*)c;
  lua_assert(table_ismap(co->t));
  stable_sort_check_idx(co->L, co->t, idx_a);
  stable_sort_check_idx(co->L, co->t, idx_b);
  TValue *ka = get_node_val(get_map_node(co->t, idx_a));
  TValue *kb = get_node_val(get_map_node(co->t, idx_b));
  int cmp = stable_sort_cmp_func_call(co, ka, kb);
  if (cmp == 0) cmp = idx_a - idx_b;
  return cmp * co->is_reverse;
}

qsort_cmp_func_define(stable_sort_cmp_map_key_up){
  FSSortMapNode *na = (FSSortMapNode*)a;
  FSSortMapNode *nb = (FSSortMapNode*)b;
  int32_t idx_a = na->idata.idx;
  int32_t idx_b = nb->idata.idx;
  FStableSortContext *co = (FStableSortContext*)c;
  lua_assert(table_ismap(co->t));
  stable_sort_check_idx(co->L, co->t, idx_a);
  stable_sort_check_idx(co->L, co->t, idx_b);
  TValue ka,kb;
  getnodekey(co->L, &ka, get_map_node(co->t, idx_a));
  getnodekey(co->L, &kb, get_map_node(co->t, idx_b));
  int cmp = stable_sort_cmp_func_call(co, &ka, &kb);
  if (cmp == 0) cmp = idx_a - idx_b;
  return cmp * co->is_reverse;
}

qsort_cmp_func_define(stable_sort_cmp_array_value_up){
  FSSortArrayNode *na = (FSSortArrayNode*)a;
  FSSortArrayNode *nb = (FSSortArrayNode*)b;
  int32_t idx_a = na->idata.idx;
  int32_t idx_b = nb->idata.idx;
  FStableSortContext *co = (FStableSortContext*)c;
  lua_assert(table_isarray(co->t));
  stable_sort_check_idx(co->L, co->t, idx_a);
  stable_sort_check_idx(co->L, co->t, idx_b);
  TValue *ka = get_array_val(co->t, idx_a);
  TValue *kb = get_array_val(co->t, idx_b);
  int cmp = stable_sort_cmp_func_call(co, ka, kb);
  if (cmp == 0) cmp = idx_a - idx_b;
  return cmp * co->is_reverse;
}
qsort_cmp_func_define(stable_sort_cmp_array_key_up){
  FSSortArrayNode *na = (FSSortArrayNode*)a;
  FSSortArrayNode *nb = (FSSortArrayNode*)b;
  int32_t idx_a = na->idata.idx;
  int32_t idx_b = nb->idata.idx;
  FStableSortContext *co = (FStableSortContext*)c;
  lua_assert(table_isarray(co->t));
  TValue ka,kb;
  setivalue(&ka, idx_a+1);
  setivalue(&kb, idx_b+1);
  int cmp = stable_sort_cmp_func_call(co, &ka, &kb);
  if (cmp == 0) cmp = idx_a - idx_b;
  return cmp * co->is_reverse;
}

qsort_cmp_func_define(stable_sort_cmp_number_up){
  FSSortIdxNode* na = (FSSortIdxNode*)a;
  FSSortIdxNode* nb = (FSSortIdxNode*)b;
  int cmp = luaV_cmpnumber(&na->id, &nb->id);
  FStableSortContext *co = (FStableSortContext*)c;
  if (cmp == 0) cmp = na->idx - nb->idx;
  return cmp * co->is_reverse;
}

static const FStableSortCmpFunc stable_sort_cmp_funcs[] = {
  NULL,
  // for map
  stable_sort_cmp_map_value_up,// 1
  stable_sort_cmp_map_key_up,
  // for getid
  stable_sort_cmp_number_up,// 3
  // for array
  stable_sort_cmp_array_value_up,// 1+3
  stable_sort_cmp_array_key_up,
  NULL
};

#define ss_idata(b,n,sz)  ((FSSortIdxNode*)((char*)b + n*sz))
#define sz_map            sizeof(FSSortMapNode)
#define sz_arr            sizeof(FSSortArrayNode)
#define sz_idx            sizeof(FSSortIdxNode)
#define sz_tab(t)         (table_isarray(t) ? sz_arr : sz_map)
#define sz_buffer(t)      (t->count * sz_tab(t))

static void _stable_sort_work(lua_State *L, void* b, Table *t,
                    FStableSortContext *co, int cmp_idx) {
  FStableSortCmpFunc cmp = stable_sort_cmp_funcs[cmp_idx];
  lua_assert(cmp != NULL && table_maxcount(t) > 1);
  int count = table_maxcount(t);
  int one_sz = sz_tab(t);
  // 填充idx
  if (cmp_idx == 3) {
    lua_assert(co->func_idx != 0 && cmp == stable_sort_cmp_number_up);// 额外还要id
    for (int i = 0; i < count; i++) {
      lua_pushvalue(L, co->func_idx);
      if (table_isarray(t)) {
        lua_pushinteger(L, i+1);
        luaA_pushvalue(L, get_array_val(t, i));
      }
      else {
        Node* lnode = get_map_node(t, i);
        TValue kk;
        getnodekey(L, &kk, lnode);
        luaA_pushvalue(L, &kk);
        luaA_pushvalue(L, get_node_val(lnode));
      }
      lua_call(L, 2, 1);
      // 之所有这么写。是因为想同时支持 float 和 int.
      TValue* id = luaA_index2value(L, -1);
      luaL_argexpected(L, ttisnumber(id), -1, "getid number");
      FSSortIdxNode* node = ss_idata(b,i,one_sz);
      node->id = *id;
      node->idx = i;
    }
    if l_unlikely(count != table_maxcount(t)){
      luaL_error(L, "getid function should not change table size.");
    }

    qsort_s(b, count, one_sz, cmp, co);
    lua_assert(count == table_maxcount(t));
  }
  else {
    for (int i = 0; i < count; i++) {
      FSSortIdxNode* node = ss_idata(b,i,one_sz);
      node->idx = i;
    }

    qsort_s(b, count, one_sz, cmp, co);
    if l_unlikely(count != table_maxcount(t)){
      luaL_error(L, "cmp function should not change table size.");// 这个也可以不报错的。
    }
  }

  // change table
  if (table_isarray(t)) {
    FSSortArrayNode* nodes = (FSSortArrayNode*)b;
    for (int i = 0; i < count; i++) {
      nodes[i].t_data = *get_array_val(t, nodes[i].idata.idx);
    }
    // memcpy 32位系统上不行
    if l_likely(sizeof(TValue) == sz_arr) {
      memcpy(t->data, b, sz_buffer(t));
    }
    else {
      for (int i = 0; i < count; i++) {
        *get_array_val(t, i) = nodes[i].t_data;
      }
    }
  }
  else {
    FSSortMapNode* nodes = (FSSortMapNode*)b;
    for (int i = 0; i < count; i++) {
      nodes[i].t_data = *get_map_node(t, nodes[i].idata.idx);
    }
    lua_assert(sizeof(Node) == sz_map);
    memcpy(t->data, b, sz_buffer(t));
    luaH_try_shrink(L, t, 0, 1, 1);
  }
}

/*doc@om
稳定排序。 table.stable_sort( t, opt1?, opt2?)
opt1
- 1   sortbyvalue   ascending order  (default)
- -1  sortbyvalue   descending order
- 2   sortbykey     ascending order
- -2  sortbykey     descending order
- function     opt2 控制函数如何使用
  - 1               cmp(value_a,value_b):number ascending order (default)
  - -1              cmp(value_a,value_b):number descending order
  - 2               cmp(key_a,key_b):number ascending order
  - -2              cmp(key_a,key_b):number descending order
  - 3               getid(k,v):number ascending order 函数返回数字id作为排序依据
  - 3               getid(k,v):number descending order 函数返回数字id作为排序依据

PS: 开始时打算替换 sort 的. 后发现比想象的难呀。
  为了不破坏 table 的结构，得复制一份内存，在外部排序，最后一次性修改整个table。
  既然已经要这样了。那就实现个稳定排序好了。同时通过额外参数。控制各种排序模式。
  使用 qsort_s 来排序。也许需要定义宏 __STDC_WANT_LIB_EXT1__
  > https://en.cppreference.com/w/c/algorithm/qsort
*/
static int stable_sort(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  int num = lua_gettop(L);
  int func_idx;
  int opt;
  if (num == 1){
    func_idx = 0;
    opt = 1;
  }
  else if (num == 2) {
    if (lua_isfunction(L, 2)){
      func_idx = 2;
      opt = 1;
    }
    else {
      func_idx = 0;
      opt = (int)luaL_checkinteger(L, 2);
      if (opt == 0 || opt < -2 || opt > 2){
        luaL_argerror(L, 2, "opt1 need be one of [1,-1,2,-2]");
      }
    }
  }
  else {
    luaL_checktype(L, 2, LUA_TFUNCTION);  /* must be a function */
    func_idx = 2;
    opt = (int)luaL_checkinteger(L, 3);
    if (opt == 0 || opt < -3 || opt > 3){
      luaL_argerror(L, 3, "opt1 need be one of [1,-1,2,-2,3,-3]");
    }
  }
  // start sort
  int cmp_idx = lua_abs(opt);
  Table* t = hvalue(luaA_index2value(L, 1));
  // skip some situation
  if (table_maxcount(t) <= 1) return 0;
  if (table_isarray(t) && func_idx == 0) {
    if (opt == -2) {
      // 倒序数组。
      for (int i = 0; i < t->count/2; i++){
        TValue tmp = *get_array_val(t, i);
        *get_array_val(t, i) = *get_array_val(t, t->count-i-1);
        *get_array_val(t, t->count-i-1) = tmp;
      }
    }
    else if(opt == 2){
      return 0;// 数组按索引排序，无用。
    }
  }
  // start work
  if (cmp_idx != 3 && table_isarray(t)){
    cmp_idx += 3;
  }
  FStableSortContext context;
  context.func_idx = func_idx;
  context.L = L;
  context.t = t;
  context.is_reverse = (opt < 0) ? -1 : 1;

  size_t buffersize = sz_buffer(t);
  if (buffersize > LUAL_BUFFERSIZE) {
    void* buffer = luaL_newbigbuffer(L, buffersize);
    _stable_sort_work(L, buffer, t, &context, cmp_idx);
  }
  else {
    luaL_Buffer b;
    void* buffer = luaL_buffinitsize(L, &b, buffersize);
    _stable_sort_work(L, buffer, t, &context, cmp_idx);
  }
  return 0;
}

static int tablib_shrink(lua_State*L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  Table* t = hvalue(luaA_index2value(L, 1));
  int resize_mem = 1;
  if (lua_gettop(L) == 2) {
    lua_Integer opt = luaL_optinteger(L, 2, 1);
    resize_mem = opt == 1 ? 0 : 1;
  }
  
  luaH_try_shrink(L, t, 1, 1, 0);
  return 0;
}

static int tablib_ismap(lua_State*L) {
  int ok = lua_ismap(L, 1);
  lua_pushboolean(L, ok);
  return 1;
}

static int tablib_isarray(lua_State*L) {
  int ok = lua_isarray(L, 1);
  lua_pushboolean(L, ok);
  return 1;
}

static int tablib_push(lua_State *L) {
  int n = lua_gettop(L);
  if (n < 2) {
    lua_pushinteger(L, 0);
    return 1;// throw error ?
  }
  luaL_checktype(L, 1, LUA_TTABLE);
  Table* t = hvalue(luaA_index2value(L, 1));
  int precount = table_count(t);
  for (int i = 1; i < n; i++){
    TValue* val = luaA_index2value(L, i+1);
    if l_likely(!ttisnil(val)) {
      int idx = table_count(t) + 1;
      luaH_setint(L, t, idx, val);
      // 不需要 invalidateTMcache
      luaC_barrierback(L, obj2gco(t), val);
    }
  }
  lua_pushinteger(L, table_count(t) - precount);// 实际push的元素个数
  return 1;
}

static int tablib_pop(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  Table* t = hvalue(luaA_index2value(L, 1));
  int n = (int)luaL_optinteger(L, 2, 1);
  if (n <= 0) {
    return 0;
  }
  n = lua_min(table_count(t),n);
  lua_checkstack(L, n);
  int i = 0;
  for (; i < n; i++) {
    TValue* slot = (TValue*)luaH_getint(t, table_count(t));
    if l_unlikely(isabstkey(slot)) {
      break;// 没办法继续弹出了
    }
    luaA_pushvalue(L, slot);
    luaH_remove(t, nodefromval(slot));// delete it
  }
  return i;
}

static int tablib_trim(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  Table* t = hvalue(luaA_index2value(L, 1));
  // 不改变内存大小。
  luaH_try_shrink(L, t, 2, 0, 0);
  return 0;
}

static int tablib_setlocksize(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  Table* t = hvalue(luaA_index2value(L, 1));
  int32_t size = (int32_t)luaL_checkinteger(L, 2);
  luaH_setlocksize(L, t, size);
  return 0;
}

static int tablib_getlocksize(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  Table* t = hvalue(luaA_index2value(L, 1));
  int32_t size = luaH_getlocksize(L, t);
  lua_pushinteger(L, size);
  return 1;
}

static const luaL_Reg tab_funcs[] = {
  {"concat", tconcat},
  {"insert", tinsert},
  {"pack", tpack},
  {"unpack", tunpack},
  {"remove", tremove},
  {"move", tmove},
  {"sort", sort},
  {"newmap", newmap},
  {"newarray", newarray},
  {"get_capacity", get_capacity},
  {"next", tablib_next},
  {"stable_sort", stable_sort},
  {"ismap", tablib_ismap},
  {"isarray", tablib_isarray},
  // 这些操作无视元表。
  {"shrink", tablib_shrink},
  {"push", tablib_push},
  {"pop", tablib_pop},
  {"trim", tablib_trim},
  //
  {"setlocksize", tablib_setlocksize},
  {"getlocksize", tablib_getlocksize},
  {NULL, NULL}
};


LUAMOD_API int luaopen_table (lua_State *L) {
  luaL_newlib(L, tab_funcs);
  return 1;
}

