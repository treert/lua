/*
** $Id: ltable.c $
** Lua tables (hash)
** See Copyright Notice in lua.h
*/

#define ltable_c
#define LUA_CORE

#include "lprefix.h"


/*
** Implementation of tables (aka arrays, objects, or hash tables).
** Tables keep its elements in two parts: an array part and a hash part.
** Non-negative integer keys are all candidates to be kept in the array
** part. The actual size of the array is the largest 'n' such that
** more than half the slots between 1 and n are in use.
** Hash uses a mix of chained scatter table with Brent's variation.
** A main invariant of these tables is that, if an element is not
** in its main position (i.e. the 'original' position that its hash gives
** to it), then the colliding element is in its own main position.
** Hence even when the load factor reaches 100%, performance remains good.
*/

#include <math.h>
#include <limits.h>
#include <memory.h>

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "lvm.h"

/* 
HashTable 的实现算法来自 dotnet。和 lua 自带的有很大差别，是个纯粹的hash表。
1. 数组部分打算单独实现个Array，感觉那样更好。【一个重要的原因是，数组空洞和数组长度有冲突，不好处理。纯粹的hash表没这个问题】
2. HashTable 的容量只增加，不减少。好处是 for in t 循环中可以修改表。【这个 for 不是lua的 pairs, 虽然lua的pairs也会有类似的效果】
*/

/// Hash Function For Data
// lua float 来自 dotnet 的实现
inline static uint32_t gethash_double(double number){
  int64_t num = *(int64_t*)(&number);
  // mylua dont care ablout NaN
  // Optimized check for IsNan() || IsZero()
  // if (((num - 1) & 0x7FFFFFFFFFFFFFFFL) >= 9218868437227405312L)
  // {
  //   // Ensure that all NaNs and both zeros have the same hash code
  //   num &= 0x7FF0000000000000L;
  // }
  return (int32_t)num ^ (int32_t)(num >> 32);
}

// lua integer
inline static uint32_t gethash_int64(int64_t num){
  return (int32_t)num ^ (int32_t)(num >> 32);
}

inline static uint32_t gethash_ptr(void* ptr){
  return (int32_t)(intptr_t)ptr;// dotnet的实现方法就是这样直接截断
}

// #define my_better_mod(hash,divisor) ((hash) % (divisor))
// #define my_better_mod(hash,divisor) ((hash) < (uint32_t)(divisor) ? (hash) : (hash) % (uint32_t)(divisor))
inline static uint32_t my_better_mod(uint32_t hash, uint32_t divisor) {
  return hash < divisor ? hash : hash % divisor;
}

#define MYLUA_MAP_USE_PRIME_SIZE 1

#if MYLUA_MAP_USE_PRIME_SIZE

/// Size And Prime
static const uint32_t s_primes[MAX_LOG_SIZE + 1] = {
    1,3,5,11,19,41,79,157,317,631,1259,2521,5039,10079,20161,40343,80611,161221,322459,644881,1289749,2579513,5158999,10317991,20635981,41271991,82543913,165087817,330175613,660351239,1320702451
};
// static const uint64_t s_primes_fastmod_multiplier[MAX_LOG_SIZE + 1] = {
//     0,6148914691236517206ull,3689348814741910324ull,1676976733973595602ull,970881267037344822ull,449920587163647601ull,233503089540627236ull,117495185182863387ull,58191621683626346ull,29234142747558719ull,14651901567680343ull,7317232873347700ull,3660794616731406ull,1830215703314769ull,914971681648210ull,457247702791304ull,228836561681527ull,114418990539133ull,57206479191803ull,28604880704672ull,14302584513506ull,7151250671623ull,3575644049110ull,1787823237461ull,893911662049ull,446955516969ull,223477945294ull,111738978739ull,55869492923ull,27934745912ull,13967373242ull,
// };

/// <summary>Performs a mod operation using the multiplier pre-computed.</summary>
/// <remarks>This should only be used on 64-bit.</remarks>
// inline static uint32_t helper_FastMod(uint32_t value, uint32_t divisor, uint64_t multiplier)
// {
//   uint32_t highbits = (uint32_t)(((((multiplier * value) >> 32) + 1) * divisor) >> 32);
//   return highbits;
// }

// // 通过 hash 值计算 bucket index
// inline static uint32_t getbucketidx(uint32_t hash, uint8_t log_size)
// {
//   lua_assert(log_size <= MAX_LOG_SIZE);
//   uint32_t d = s_primes[log_size];
//   uint64_t m = s_primes_fastmod_multiplier[log_size];
//   return helper_FastMod(hash, d, m);
// }

#define getmapindexmemsize(lsize) (sizeof(int32_t)*s_primes[lsize])

// #define getbucketbyhash(t,hash) (getbucketstart(t) + getbucketidx(hash,t->lsizenode))
// #define getbucketbyhash(t,hash) (getbucketstart(t) + helper_FastMod(hash,t->capacity, t->fastmoder))
// #define getbucketbyhash(t,hash) (getbucketstart(t) + hash%t->capacity)
#define getbucketbyhash(t,hash)         (getbucketstart(t) + my_better_mod(hash,s_primes[t->lsizenode]))

#else /* MYLUA_MAP_USE_PRIME_SIZE */

#define getmapindexmemsize(lsize) (sizeof(int32_t)*twoto(lsize))

#define getbucketbyhash(t,hash)         (getbucketstart(t) + my_better_mod(hash,((twoto(t->lsizenode)-1)|1)))

#endif /* MYLUA_MAP_USE_PRIME_SIZE */

#define getmapmemsize(lsize) (sizeof(Node)*twoto(lsize) + getmapindexmemsize(lsize))
#define getarraymemsize(lsize) (sizeof(TValue)*twoto(lsize))

// get bucket index start ptr. bucket index store after node
#define getbucketstart(t)       ((int32_t*)(get_map_ptr(t) + twoto(t->lsizenode)))

#define getbucketbyhash_pow2(t, hash)   (getbucketstart(t) + (hash&(twoto(t->lsizenode)-1)))

#define getbucket_byint(t,num)    getbucketbyhash(t, gethash_int64(num))
#define getbucket_byflt(t,flt)    getbucketbyhash(t, gethash_double(flt))
#define getbucket_bystr(t,str)    getbucketbyhash_pow2(t, (str)->hash)

// generic hash function
static int32_t* getbucket_generic(Table *t, const TValue *key) {
  switch (ttypetag(key)) {
    case LUA_VNUMINT: {
      return getbucket_byint(t, ivalue(key));
    }
    case LUA_VNUMFLT: {
      return getbucket_byflt(t, fltvalue(key));
    }
    case LUA_VSHRSTR: {
      return getbucket_bystr(t, tsvalue(key));
    }
    case LUA_VLNGSTR: {
      int32_t hash = luaS_hashlongstr(tsvalue(key));
      return getbucketbyhash_pow2(t, hash);
    }
    case LUA_VFALSE:
      return getbucketbyhash_pow2(t, 0);
    case LUA_VTRUE:
      return getbucketbyhash_pow2(t, 1);
    // case LUA_VLIGHTUSERDATA: {
    //   void *p = pvalue(key);
    //   return gethash_ptr(p);
    // }
    // case LUA_VLCF: {
    //   lua_CFunction f = fvalue(key);
    //   return gethash_ptr((void*)f);
    // }
    default: {
      void *ptr = ptrvalue(key);
      uint32_t hash = gethash_ptr(ptr);
      return getbucketbyhash(t, hash);
    }
  }
}

static const Node dummynode_[2] = {
  {
  {{NULL}, LUA_VNIL,  /* value's value and type */
   LUA_VNIL, 0, {NULL}}  /* key type, next, and key value */
  },
  {
    .i_val = {.value_={.i=-1},-1}
  }
};

#define dummynode		((void*)(&dummynode_[0]))
#define isdummy(t)  ((t)->data == dummynode)

static const TValue absentkey = {ABSTKEYCONSTANT};


/*
** Check whether key 'k1' is equal to the key in node 'n2'. This
** equality is raw, so there are no metamethods. Floats with integer
** values have been normalized, so integers cannot be equal to
** floats. It is assumed that 'eqshrstr' is simply pointer equality, so
** that short strings are handled in the default case.
*/
static int equalkey (const TValue *k1, const Node *n2) {
  if (rawtt(k1) != keytt(n2))
   return 0;  /* cannot be same key */
  switch (keytt(n2)) {
    case LUA_VNIL: case LUA_VFALSE: case LUA_VTRUE:
      return 1;
    case LUA_VNUMINT:
      return (ivalue(k1) == keyival(n2));
    case LUA_VNUMFLT:
      return luai_numeq(fltvalue(k1), fltvalueraw(keyval(n2)));
    // case LUA_VLIGHTUSERDATA:
    //   return pvalue(k1) == pvalueraw(keyval(n2));
    // case LUA_VLCF:
    //   return fvalue(k1) == fvalueraw(keyval(n2));
    case ctb(LUA_VLNGSTR):
      return luaS_eqlngstr(tsvalue(k1), keystrval(n2));
    default:
      return ptrvalue(k1) == ptrvalueraw(keyval(n2));
      // return gcvalue(k1) == gcvalueraw(keyval(n2));
  }
}

/*
** Search function for integers.
*/
static const TValue* getint_node (Table *t, lua_Integer key) {
  lua_assert(get_map_ptr(t) != NULL);
  int32_t* bucket = getbucket_byint(t, key);
  int32_t nx = *bucket;
  while(nx >= 0){
    Node* n = gnode(t,nx);
    if (keyisinteger(n) && keyival(n) == key){
      return gval(n);
    }
    else{
      nx = gnext(n);
    }
  }
  return &absentkey;
}


/*
** search function for short strings
*/
static const TValue* getshortstr_node (Table *t, TString *key) {
  lua_assert(get_map_ptr(t) != NULL);
  int* bucket = getbucket_bystr(t, key);
  int nx = *bucket;
  while(nx >= 0){
    Node* n = gnode(t,nx);
    if (keyisshrstr(n) && eqshrstr(keystrval(n), key)){
      return gval(n);
    }
    else{
      nx = gnext(n);
    }
  }
  return &absentkey;
}

/*
** "Generic" get version.
*/
static const TValue* getgeneric_node(Table *t, const TValue *key) {
  lua_assert(get_map_ptr(t) != NULL);
  lua_assert(!ttisnil(key));
  int32_t* bucket = getbucket_generic(t, key);
  int32_t nx = *bucket;
  while(nx >= 0){
    Node* n = gnode(t,nx);
    if (equalkey(key, n)){
      return gval(n);
    }
    else{
      nx = gnext(n);
    }
  }
  return &absentkey;
}

int luaH_next (lua_State *L, Table *t, StkId key) {
  // doc@om 发现个问题。float 类型的 1.0 也会不支持，不过不打算做什么。
  if(table_isarray(t)){
    // for array
    lua_Integer idx = INT32_MAX;
    if (ttisnil(s2v(key))){
      idx = 0;
    }
    else if (ttisinteger(s2v(key))){
      idx = ivalue(s2v(key));
    }
    // skip empty value
    for(;idx < t->count; idx++){
      if (!isempty(get_array_val(t,idx))) {
        setivalue(s2v(key), idx + 1);
        setobj2s(L, key + 1, get_array_val(t, idx));
        return 1;
      }
    }
    return 0;  /* no more elements */
  }
  // for map
  Node* node;
  if (!ttisnil(s2v(key))){
    const TValue* val = getgeneric_node(t, s2v(key));
    if (l_unlikely(isabstkey(val))){
      luaG_runerror(L, "invalid key to 'next'");  /* key not found */ // 兼容 lua
      return 0;
    }
    node = nodefromval(val) + 1;
  }
  else{
    node = gnode(t, 0);// 开始遍历
  }
  Node* limit = gnodelast(t);
  for (; node < limit; node++){
    if (!isempty(gval(node))) {  /* a non-empty entry? */
      getnodekey(L, s2v(key), node);
      setobj2s(L, key + 1, gval(node));
      return 1;
    }
  }
  return 0;  /* no more elements */
}

int32_t luaH_itor_next (lua_State *L, Table *t, int32_t itor_idx, StkId ret_idx) {
  lua_assert(itor_idx >= 0);
  if(table_isarray(t)){
    // for array
    for(;itor_idx < t->count; itor_idx++){
      if(!isempty(get_array_val(t, itor_idx))){
        setivalue(s2v(ret_idx), itor_idx + 1);
        setobj2s(L, ret_idx + 1, get_array_val(t, itor_idx));
        return itor_idx+1;
      }
    }
    return -1;
  }
  // for map
  for(;itor_idx < t->count; itor_idx++){
    Node* node = gnode(t, itor_idx);
    if(!isempty(gval(node))){
      getnodekey(L, s2v(ret_idx), node);
      setobj2s(L, ret_idx + 1, gval(node));
      return itor_idx+1;
    }
  }
  return -1;
}

static void rehash_map(Table *t){
  lua_assert(table_ismap(t));
  if (isdummy(t)) return;
  lua_State*L = NULL;
  memset(getbucketstart(t),-1,getmapindexmemsize(t->lsizenode));
  for (int32_t i = 0; i < table_maxcount(t); i++) {
    Node *n = gnode(t, i);
    if(!isempty(gval(n))){
      TValue key;
      getnodekey(L, &key, n);
      int32_t* bucket = getbucket_generic(t, &key);
      gnext(n) = *bucket;
      *bucket = i;
    }
  }
}

// 重新选择数组的内存大小。【只会减小内存】
static void resize_table_mem(lua_State *L, Table *t, int need_rehash_map){
  if (isdummy(t)) return;// 不可能再减少了
  
  if (t->count > 0) {
    int lsize = luaO_ceillog2(t->count);// 肯定有效的
    if (lsize == t->lsizenode) return;// 没变化
    lua_assert(lsize < t->lsizenode);

    size_t new_sz = table_isarray(t) ? getarraymemsize(lsize): getmapmemsize(lsize);
    size_t old_sz = table_isarray(t) ? getarraymemsize(t->lsizenode): getmapmemsize(t->lsizenode);
    t->data = luaM_realloc(L, t->data, old_sz, new_sz);
    t->lsizenode = lsize;
  }
  else {
    size_t old_sz = table_isarray(t) ? getarraymemsize(t->lsizenode): getmapmemsize(t->lsizenode);
    luaM_freemem(L, t->data, old_sz);
    t->data = dummynode;
    t->lsizenode = 0;// no need
  }
  
  if (need_rehash_map && table_ismap(t)) {
    rehash_map(t);
  }
}

/*
  增加容量，**不改变 count**.
*/
void luaH_addsize (lua_State *L, Table *t, int32_t addsize){
  if (addsize <= 0) return;
  int32_t oldcount = table_count(t);
  int32_t newcount = oldcount + addsize;
  int lsize = luaO_ceillog2(newcount);
  if(lsize > MAX_LOG_SIZE){
    luaG_runerror(L, "table overflow");
  }
  if(!isdummy(t)){
    if(lsize <= t->lsizenode) return;// 容量足够
  }
  // for array
  if (table_isarray(t)){
    if (isdummy(t)) {
      size_t newsize = getarraymemsize(lsize);
      t->data = luaM_realloc(L, NULL, 0, newsize);
    }
    else {
      size_t oldsize = getarraymemsize(t->lsizenode);
      size_t newsize = getarraymemsize(lsize);
      t->data = luaM_realloc(L, t->data, oldsize, newsize);
    }
    t->lsizenode = lsize;
    // not need fill nil
    return;
  }
  // for map
  int32_t maxcount = table_maxcount(t);
  if (isdummy(t)) {
    size_t newsize = getmapmemsize(lsize);
    t->data = luaM_realloc(L, NULL, 0, newsize);
  }
  else {
    size_t oldsize = getmapmemsize(t->lsizenode);
    size_t newsize = getmapmemsize(lsize);
    t->data = luaM_realloc(L, t->data, oldsize, newsize);
  }
  t->lsizenode = lsize;
  // rebuild bucket idx. not shrink map. 避免遍历的时候修改数组导致遍历出问题
  rehash_map(t);
}

void luaH_reserve (lua_State *L, Table *t, int32_t size) {
  int32_t count = table_count(t);
  luaH_addsize(L, t, size - count);
}

/*
修改表格的大小。map 和 array 有很大差别.
1. map： 调用 luaH_reserve 提前腾出容量
2. array: 会真的修改数组的大小。
 */
void luaH_resize (lua_State *L, Table *t, int32_t new_size) {
  if l_unlikely(new_size < 0) {
    luaG_runerror(L, "table.resize need >= 0, get %d", new_size);
    return;
  }
  if l_unlikely(table_ismap(t)) {
    luaH_reserve(L, t, new_size);
    return;
  }
  lua_assert(t->freecount == 0);
  int count = t->count;
  if (count >= new_size) {
    t->count = new_size;
    // do nothing else
  }
  else {
    luaH_addsize(L, t, new_size - t->count);
    for (int i = t->count; i < new_size; i++) {
      setnilvalue(get_array_val(t, i));
    }
    t->count = new_size;
  }
}

void luaH_setlocksize (lua_State *L, Table *t, int32_t size) {
  if (table_isarray(t)){
    // 其实应该校验下的。特别是 0 < size < count 时。还没想好，先这么着
    t->freelist = size;
  }
}

int32_t luaH_getlocksize (lua_State *L, Table *t) {
  if (table_isarray(t)) {
    return t->freelist;
  }
  return 0;
}

void luaH_try_shrink (lua_State *L, Table *t, int also_for_array, int resize_mem, int force_rehash) {
  if(table_maxcount(t) == 0) return;
  if (table_ismap(t)){
    int32_t maxcount = table_maxcount(t);
    int32_t valid_count = 0;
    if l_likely(t->freecount == 0) {
      valid_count = maxcount;
    }
    else {
      for (valid_count = 0; valid_count < maxcount; valid_count++) {
        Node *n = gnode(t, valid_count);
        if l_unlikely(isempty(gval(n)))
          break;
      }
      for (int i = valid_count + 1; i < maxcount; i++){
        Node *n = gnode(t, i);
        if (!isempty(gval(n))){
          *gnode(t, valid_count) = *n;
          valid_count++;
        }
      }
    }
    int need_rehash = force_rehash || (valid_count < maxcount);// count 变化，说明有移动发生
    t->count = valid_count;
    t->freecount = 0;
    t->freelist = 0;// no need
    if (resize_mem && L) {
      resize_table_mem(L, t, need_rehash);
    }
    else {
      if (need_rehash) {
        rehash_map(t);
      }
    }
    return;
  }
  // for array
  if (also_for_array == 1) {
    int32_t valid_count = 0;
    for (; valid_count < t->count; valid_count++) {
      if l_unlikely(isempty(get_array_val(t, valid_count))){
        break;
      }
    }
    for (int i = valid_count + 1; i < t->count; i++) {
      TValue *n = get_array_val(t, i);
      if (!isempty(n)) {
        *get_array_val(t, valid_count) = *n;
        valid_count++;
      }
    }
    if (valid_count < t->count) {
      t->count = valid_count;
    }
  }
  else if (also_for_array == 2) {
    while(t->count >= 0 && isempty(get_array_val(t, t->count -1))){
      t->count--;
    }
  }
  if (resize_mem && L) {
    resize_table_mem(L, t, 0);
  }
}

/*
** }=============================================================
*/


Table *luaH_new (lua_State *L) {
  GCObject *o = luaC_newobj(L, LUA_VTABLE, sizeof(Table));
  Table *t = gco2t(o);
  t->metatable = NULL;
  t->flags = cast_byte(maskflags);  /* table has no metamethod fields */
  t->lsizenode = 0;
  t->count = 0;
  t->freecount = 0;
  t->freelist = 0;
  t->data = dummynode;  /* lua use common 'dummynode', mylua just set NULL*/
  return t;
}

Table *luaH_newarray (lua_State *L) {
  GCObject *o = luaC_newobj(L, LUA_VArray, sizeof(Table));
  Table *t = gco2t(o);
  t->metatable = NULL;
  t->flags = cast_byte(maskflags);  /* array has no metamethod fields */
  t->lsizenode = 0;
  t->count = 0;
  t->freecount = 0;
  t->freelist = 0;
  t->data = dummynode;  /* lua use common 'dummynode', mylua just set NULL*/
  return t;
}


void luaH_free (lua_State *L, Table *t) {
  if (!isdummy(t)){
    size_t size = table_isarray(t) ? getarraymemsize(t->lsizenode) : getmapmemsize(t->lsizenode);
    luaM_freemem(L, t->data, size);
  }
  luaM_free(L, t);
}

/*
** Search function for integers.
*/
const TValue *luaH_getint (Table *t, lua_Integer key) {
  if (table_ismap(t)) {
    return getint_node(t, key);
  }
  // for array
  if ( 0 < key && key <= t->count){
    return get_array_val(t, key-1);
  }
  return &absentkey;
}


/*
** search function for short strings
*/
const TValue *luaH_getshortstr (Table *t, TString *key) {
  if (table_ismap(t)) {
    return getshortstr_node(t, key);
  }
  return &absentkey;
}

/*
** search function for strings
*/
const TValue *luaH_getstr (Table *t, TString *key) {
  if (key->tt == LUA_VSHRSTR) {
    return luaH_getshortstr(t, key);
  }
  if (table_ismap(t) && t->data) {
    TValue ko;
    setsvalue(cast(lua_State *, NULL), &ko, key);
    return getgeneric_node(t, &ko);
  }
  return &absentkey;
}


/*
** main search function
*/
const TValue *luaH_get (Table *t, const TValue *key) {
  if (table_ismap(t)) {
    switch (ttypetag(key)) {
      case LUA_VSHRSTR:
        return getshortstr_node(t, tsvalue(key));
      case LUA_VNUMINT:
        return getint_node(t, ivalue(key));
      case LUA_VNIL: return &absentkey;// throw error?
      case LUA_VNUMFLT: {
        lua_Integer k;
        if (luaV_flttointeger(fltvalue(key), &k, F2Ieq)) /* integral index? */
        {
          return getint_node(t, k);/* use specialized version */
        }
        /* else... */
      }  /* FALLTHROUGH */
      default:
        return getgeneric_node(t, key);
    }
  }
  // for array
  lua_Integer idx = 0;
  if (ttypetag(key) == LUA_VNUMINT) {
    idx = ivalue(key);
  }
  else if (ttypetag(key) == LUA_VNUMFLT) {
    luaV_flttointeger(fltvalue(key), &idx, F2Ieq); /* integral index? */
  }
  if ( 0 < idx && idx <= t->count){
    return get_array_val(t, idx-1);
  }
  return &absentkey;
}


/*
** Finish a raw "set table" operation, where 'slot' is where the value
** should have been (the result of a previous "get table").
** Beware: when using this function you probably need to check a GC
** barrier and invalidate the TM cache.
*/
void luaH_finishset (lua_State *L, Table *t, const TValue *key,
                                   const TValue *slot, TValue *value) {
  lua_assert(slot != NULL);
  if (table_isarray(t)) {
    // for array
    if (isabstkey(slot)){
      lua_Integer idx = 0;
      if (ttypetag(key) == LUA_VNUMINT) {
        idx = ivalue(key);
      }
      else if (ttypetag(key) == LUA_VNUMFLT) {
        if (!luaV_flttointeger(fltvalue(key), &idx, F2Ieq)){
          luaG_runerror(L, "array index is float, expect integer");
        }
      }
      else{
        luaG_typeerror(L, key, "index array with");
      }

      luaH_newarrayitem(L, t, idx, value);
    }
    else {
      setobj2array(L, cast(TValue *, slot), value);// just set, dont care about nil
    }
    return;
  }
  // for map
  if (isabstkey(slot)){
    luaH_newkey(L, t, key, value);
  }
  else{
    if(ttisnil(value)){
      luaH_remove(t, nodefromval(slot));
    }
    else{
      setobj2t(L, cast(TValue *, slot), value);
    }
  }
}

/*
** beware: when using this function you probably need to check a GC
** barrier and invalidate the TM cache.
*/
void luaH_set (lua_State *L, Table *t, const TValue *key, TValue *value) {
  const TValue *slot = luaH_get(t, key);
  luaH_finishset(L, t, key, slot, value);
}

void luaH_setint (lua_State *L, Table *t, lua_Integer key, TValue *value) {
  if(table_isarray(t)){
    // for array
    if ( 0 < key && key <= t->count){
      setobj2array(L, get_array_val(t, key-1), value);// just set, dont care about nil
    }
    else {
      luaH_newarrayitem(L, t, key, value);
    }
    return;
  }
  // for map
  const TValue *slot = luaH_getint(t, key);
  TValue k;
  setivalue(&k, key);
  luaH_finishset(L, t, &k, slot, value);
}

// 添加 (key,value), key should not exsit. maybe need barrier value.
void luaH_newkey (lua_State *L, Table *t, const TValue *key, TValue *value) {
  lua_assert(table_ismap(t));
  if(ttisnil(value)) return;// do nothing

  TValue aux;
  if (l_unlikely(ttisnil(key)))
    luaG_runerror(L, "table index is nil");
  else if (ttisfloat(key)) {
    lua_Number f = fltvalue(key);
    lua_Integer k;
    if (luaV_flttointeger(f, &k, F2Ieq)) {  /* does key fit in an integer? */
      setivalue(&aux, k);
      key = &aux;  /* insert it as an integer */
    }
    else if (l_unlikely(luai_numisnan(f)))
      luaG_runerror(L, "table index is NaN"); // 如果不这么做，就得修改 equalkey里面float的部分了。
  }

  Node* nodes = get_map_ptr(t);
  Node* newnode;
  if (t->freecount > 0) {
    newnode = nodes + t->freelist;
    t->freelist = gnext(newnode);
    t->freecount--;
  }
  else {
    if(isdummy(t) || t->count == twoto(t->lsizenode)){
      // grow hash
      luaH_addsize(L, t, 1);
      nodes = get_map_ptr(t);
    }
    newnode = nodes + t->count;
    t->count++;
  }
  int32_t* bucket = getbucket_generic(t, key);
  gnext(newnode) = *bucket;
  *bucket = (int32_t)(newnode - nodes);
  setnodekey(L, newnode, key);
  luaC_barrierback(L, obj2gco(t), key);
  setobj2t(L, gval(newnode), value);
}

void luaH_newarrayitem (lua_State *L, Table *t, lua_Integer idx, TValue *value) {
  lua_assert(table_isarray(t));
  // 做了极大的限制，array只能一个一个的加
  int32_t newcount = (int)idx;
  if l_unlikely(t->count+1 != newcount) {
    luaG_runerror(L, "array can only add at last, may use table.resize before.");
    return;
  }
  // grow array
  if (isdummy(t) || newcount > twoto(t->lsizenode)){
    luaH_addsize(L, t, 1);
  }
  t->count = newcount;
  setobj2array(L, get_array_val(t, newcount-1), value);
}

/* mark an entry as empty. only luaH_remove can use*/
// #define setempty(v)		settt_(v, LUA_VEMPTY)

void luaH_remove (Table *t, Node *node) {
  lua_assert(!isdummy(t));
  TValue key;
  getnodekey(cast(lua_State*,NULL), &key, node);
  int32_t* bucket = getbucket_generic(t, &key);
  Node* nodes = get_map_ptr(t);
  Node* prenode = NULL;
  int32_t nx = *bucket;
  while(nx >= 0){
    Node* n = nodes + nx;
    if (n == node) {
      break;
    }
    else {
      prenode = n;
      nx = gnext(n);
    }
  }
  lua_assert(nx >= 0);
  // remove from bucket list
  if (prenode == NULL){
    *bucket = gnext(node);
  }
  else{
    gnext(prenode) = gnext(node);
  }
  // 清理干净。依赖的地方：stable_sort 里的比较函数。
  setnilvalue(gval(node));// 之气那使用的 setempty. mylua 还是不用的好。
  setnilkey(node);
  // add to freelist
  t->freecount++;
  gnext(node) = t->freelist;
  t->freelist = nx;
}

lua_Unsigned luaH_getn (Table *t) {
  return table_count(t);
}