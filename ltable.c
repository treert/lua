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
把lua的table分拆成 array + map, 并且给 map 配备的遍历索引。

lua 本身的table实现还是挺好。有些缺点
1. 数组空洞和数组长度有冲突，不好处理。
2. table 的hash不分不能排序。不能保留插入顺序作为遍历顺序。

拆分成 array + map 主要是为了优化这些缺点。

PS: 开始时 map 部分的实现使用的时 dotnet 的 dictionary 的实现方法。发现不如lua的快。经过一份考虑。改回lua的实现方法。
*/

/// Hash Function For Data. Come From Donet
// lua float 来自 dotnet 的实现
inline static int32_t gethash_double(double number){
  const int64_t num = *(int64_t*)(&number);
  // lua do not accept NaN as key
  // Optimized check for IsNan() || IsZero()
  // if (((num - 1) & 0x7FFFFFFFFFFFFFFFL) >= 9218868437227405312L)
  // {
  //   // Ensure that all NaNs and both zeros have the same hash code
  //   num &= 0x7FF0000000000000L;
  // }
  return (int32_t)num ^ (int32_t)(num >> 32);
}

// lua integer
inline static int32_t gethash_int64(int64_t num){
  return (int32_t)num ^ (int32_t)(num >> 32);
}

inline static int32_t gethash_ptr(void* ptr){
  return (int32_t)(intptr_t)ptr;// dotnet的实现方法就是这样直接截断
}

// generic hash function
static int32_t gethash(const TValue *key) {
  switch (ttypetag(key)) {
    case LUA_VNUMINT: {
      lua_Integer i = ivalue(key);
      return gethash_int64(i);
    }
    case LUA_VNUMFLT: {
      lua_Number n = fltvalue(key);
      return gethash_double(n);
    }
    case LUA_VSHRSTR: {
      TString *ts = tsvalue(key);
      return ts->hash;
    }
    case LUA_VLNGSTR: {
      TString *ts = tsvalue(key);
      return luaS_hashlongstr(ts);
    }
    case LUA_VFALSE:
      return 0;
    case LUA_VTRUE:
      return 1;
    // case LUA_VLIGHTUSERDATA: {
    //   const void *p = pvalue(key);
    //   return gethash_ptr(p);
    // }
    // case LUA_VLCF: {
    //   lua_CFunction f = fvalue(key);
    //   return gethash_ptr((void*)f);
    // }
    default: {
      void*ptr = ptrvalue(key);
      return gethash_ptr(ptr);
      // GCObject *o = gcvalue(key);
      // return gethash_ptr(o);
    }
  }
}

/// Size And Prime
static const int32_t s_primes[MAX_LOG_SIZE + 1] = {
    1,3,5,11,19,41,79,157,317,631,1259,2521,5039,10079,20161,40343,80611,161221,322459,644881,1289749,2579513,5158999,10317991,20635981,41271991,82543913,165087817,330175613,660351239,1320702451
};
static const uint64_t s_primes_fastmod_multiplier[MAX_LOG_SIZE + 1] = {
    0,6148914691236517206ull,3689348814741910324ull,1676976733973595602ull,970881267037344822ull,449920587163647601ull,233503089540627236ull,117495185182863387ull,58191621683626346ull,29234142747558719ull,14651901567680343ull,7317232873347700ull,3660794616731406ull,1830215703314769ull,914971681648210ull,457247702791304ull,228836561681527ull,114418990539133ull,57206479191803ull,28604880704672ull,14302584513506ull,7151250671623ull,3575644049110ull,1787823237461ull,893911662049ull,446955516969ull,223477945294ull,111738978739ull,55869492923ull,27934745912ull,13967373242ull,
};

/// <summary>Performs a mod operation using the multiplier pre-computed.</summary>
/// <remarks>This should only be used on 64-bit.</remarks>
inline static uint32_t helper_FastMod(uint32_t value, uint32_t divisor, uint64_t multiplier)
{
  uint32_t highbits = (uint32_t)(((((multiplier * value) >> 32) + 1) * divisor) >> 32);
  return highbits;
}

inline static Node* getnode_byhash(const Table *t, int32_t hash) {
  return get_map_ptr(t) + helper_FastMod(hash, t->capacity, t->fastmoder);
}

l_sinline getnewcapacity(lua_State *L, int32_t size) {
  for (int i = 0; i < MAX_LOG_SIZE + 1; i++){
    if (s_primes[i] >= size) return s_primes[i];
  }
  luaG_runerror(L, "table overflow");
  return 0;
}

#define getnode_byint(t,num)        (getnode_byhash(t, gethash_int64(num)))
#define getnode_byshortstr(t,str)   (getnode_byhash(t, (str)->hash))

// for array
#define getarraymemsize(lsize) (sizeof(TValue)*twoto(lsize))


// #define dummynode		(&dummynode_)

// static const Node dummynode_ = {
//   {{NULL}, LUA_VEMPTY,  /* value's value and type */
//    LUA_VNIL, 0, {NULL}}  /* key type, next, and key value */
// };


static const TValue absentkey = {ABSTKEYCONSTANT};

// map 数量很少时。不用 NodeInfo
#define MAP_MINI_SIZE   4

static Node* map_getfreepos (Table *t) {
  if (table_cap(t) != 0){
    Node* data = get_map_ptr(t);
    if (table_cap(t) <= MAP_MINI_SIZE) {
      Node* lastfree = get_map_node(t, table_cap(t));
      while (lastfree > data)
      {
        lastfree--;
        if (keyisnil(lastfree)){
          return lastfree;
        }
      }
    }
    else {
      Node* info = data + table_cap(t);
      while (info->lastfree > data)
      {
        info->lastfree--;
        if (keyisnil(info->lastfree)){
          return info->lastfree;
        }
      }
    }
  }
  return NULL;
}

static int32_t map_getcount (Table *t) {
  if (table_cap(t) != 0){
    if (table_cap(t) <= MAP_MINI_SIZE) {
      int count = 0;
      for (int i = 0; i < table_cap(t); i++) {
        count += !isempty(gval(get_map_node(t, i)));
      }
      return count;
    }
    else {
      Node* info = get_map_node(t, table_cap(t));
      return info->count;
    }
  }
  return 0;
}

inline static void map_add_one(Table *t, Node *n) {
  if (table_cap(t) > MAP_MINI_SIZE) {
    Node* info = get_map_node(t, table_cap(t));
    info->count++;
  }
}

inline static void map_del_one(Table *t, Node *n) {
  if (table_cap(t) > MAP_MINI_SIZE) {
    Node* info = get_map_node(t, table_cap(t));
    info->count--;
  }
}

/*
** returns the 'main' position of an element in a table (that is,
** the index of its hash value).
*/
l_sinline Node *mainpositionTV (const Table *t, const TValue *key) {
  const uint32_t hash = gethash(key);
  return getnode_byhash(t, hash);
}

l_sinline Node *mainpositionfromnode (const Table *t, Node *nd) {
  if (keyisdead(nd)) {
    return getnode_byhash(t, deadkeyhash(nd));
  }
  TValue key;
  getnodekey(cast(lua_State *, NULL), &key, nd);
  return mainpositionTV(t, &key);
}

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
** "Generic" get version.
*/
static const TValue* map_getgeneric(Table *t, const TValue *key) {
  Node* nodes = get_map_ptr(t);
  Node *n = mainpositionTV(t, key);
  for (;;) {  /* check whether 'key' is somewhere in the chain */
    if (equalkey(key, n))
      return gval(n);  /* that's it */
    else {
      int nx = gnext(n);
      if (nx == 0) break;/* not found */
      n += nx;
    }
  }
  return &absentkey;
}

int luaH_next (lua_State *L, Table *t, StkId key) {
  if(table_cap(t) == 0) return 0;
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
    for(;idx < array_count(t); idx++){
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
    const TValue *v = map_getgeneric(t, s2v(key));
    if (l_unlikely(isabstkey(v))){
      luaG_runerror(L, "invalid key to 'next'");  /* key not found */ // 兼容 lua
      return 0;
    }
    node = nodefromval(v);
    node ++;
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
  if (itor_idx < 0 ) itor_idx = 0;
  if(table_isarray(t)){
    // for array
    for(;itor_idx < t->arr_count; itor_idx++){
      if(!isempty(get_array_val(t, itor_idx))){
        setivalue(s2v(ret_idx), itor_idx + 1);
        setobj2s(L, ret_idx + 1, get_array_val(t, itor_idx));
        return itor_idx+1;
      }
    }
    return -1;
  }
  // for map
  for(;itor_idx < t->capacity; itor_idx++){
    Node* node = gnode(t, itor_idx);
    if(!isempty(gval(node))){
      getnodekey(L, s2v(ret_idx), node);
      setobj2s(L, ret_idx + 1, gval(node));
      return itor_idx+1;
    }
  }
  return -1;
}

/*
  增加容量，**不改变 count**.
*/
void luaH_addsize (lua_State *L, Table *t, int32_t addsize){
  if (addsize <= 0) return;
  // for array
  if (table_isarray(t)){
    int32_t oldcount = array_count(t);
    int32_t newcount = oldcount + addsize;
    int32_t newcapacity = getnewcapacity(L, newcount);
    if (newcapacity <= t->capacity) return;
    size_t oldsize = t->capacity * sizeof(TValue);
    size_t newsize = newcapacity * sizeof(TValue);
    t->data = luaM_realloc(L, t->data, oldsize, newsize);
    t->capacity = newcapacity;
    // not need fill nil
    return;
  }
  // for map
  {
    int32_t oldcount = table_cap(t);
    int32_t newcount = oldcount + addsize;
    Table oldt = *t;  /* to keep the new hash part */
    {
      t->capacity = getnewcapacity(L, newcount);
      t->fastmoder = UINT64_MAX/t->capacity + 1;
      Node* data = luaM_newvector(L, t->capacity+1, Node);
      t->data = (void*)data;
      for (int i = 0; i < t->capacity; i++) {
        Node* n = data+i;
        gnext(n) = 0;
        setnilkey(n);
        setnilvalue(gval(n));
      }
      Node* info = data + t->capacity;
      info->count = 0;
      // info->used_count = 0;
      info->lastfree = info;
    }
    if (oldcount > 0) {
      // reinsert
      for (int i = 0; i < oldcount; i++) {
        Node*old = gnode(&oldt, i);
        if (!isempty(gval(old))) {
          /* doesn't need barrier/invalidate cache, as entry was
            already present in the table */
          TValue k;
          getnodekey(L, &k, old);
          luaH_set(L, t, &k, gval(old));
        }
      }
      Node* olddata = oldt.data;
      luaM_freearray(L, olddata, oldt.capacity+1);
    }
  }
}

void luaH_reserve (lua_State *L, Table *t, int32_t size) {
  int32_t count = table_cap(t);
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
  int count = t->arr_count;
  if (count >= new_size) {
    t->arr_count = new_size;
    // do nothing else
  }
  else {
    luaH_addsize(L, t, new_size - t->arr_count);
    for (int i = t->arr_count; i < new_size; i++) {
      setnilvalue(get_array_val(t, i));
    }
    t->arr_count = new_size;
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
  t->data = NULL;  /* lua use common 'dummynode', mylua just set NULL*/
  t->capacity = 0;
  t->fastmoder = 0;
  return t;
}

Table *luaH_newarray (lua_State *L) {
  GCObject *o = luaC_newobj(L, LUA_VArray, sizeof(Table));
  Table *t = gco2t(o);
  t->metatable = NULL;
  t->flags = cast_byte(maskflags);  /* array has no metamethod fields */
  t->data = NULL;  /* lua use common 'dummynode', mylua just set NULL*/
  t->capacity = 0;
  t->fastmoder = 0;
  return t;
}


void luaH_free (lua_State *L, Table *t) {
  if (t->data != NULL){
    size_t size = table_isarray(t) ? 
        sizeof(TValue) * t->capacity : sizeof(Node) * (t->capacity+1);
    luaM_freemem(L, t->data, size);
  }
  luaM_free(L, t);
}

/*
** Search function for integers.
*/
const TValue *luaH_getint (Table *t, lua_Integer key) {
  if l_unlikely(table_cap(t) == 0) return &absentkey;
  if(table_isarray(t)){
    // for array
    if (0 < key && key <= t->arr_count){
      return get_array_val(t, key-1);
    }
    return &absentkey;
  }
  // for map
  Node* n = getnode_byint(t, key);
  for (;;) {
    if (keyisinteger(n) && keyival(n) == key)
      return gval(n);
    else {
      int nx = gnext(n);
      if (nx == 0) break;
      n += nx;
    }
  }
  return &absentkey;
}


/*
** search function for short strings
*/
const TValue *luaH_getshortstr (Table *t, TString *key) {
  if l_unlikely(table_cap(t) == 0) return &absentkey;
  if(table_isarray(t)){
    return &absentkey;
  }
  // for map
  lua_assert(key->tt == LUA_VSHRSTR);
  Node* n = getnode_byshortstr(t, key);
  for (;;) {
    if (keyisshrstr(n) && eqshrstr(keystrval(n), key))
      return gval(n);  /* that's it */
    else {
      int nx = gnext(n);
      if (nx == 0)
        return &absentkey;  /* not found */
      n += nx;
    }
  }
  return &absentkey;
}

/*
** search function for strings
*/
const TValue *luaH_getstr (Table *t, TString *key) {
  if (key->tt == LUA_VSHRSTR)
    return luaH_getshortstr(t, key);

  if l_unlikely(table_cap(t) == 0) return &absentkey;
  if (table_isarray(t)){
    return &absentkey;
  }
  // for map
  TValue ko;
  setsvalue(cast(lua_State *, NULL), &ko, key);
  return map_getgeneric(t, &ko);
}

/*
** main search function
*/
const TValue *luaH_get (Table *t, const TValue *key) {
  switch (ttypetag(key)) {
    case LUA_VSHRSTR: return luaH_getshortstr(t, tsvalue(key));
    case LUA_VNUMINT: return luaH_getint(t, ivalue(key));
    case LUA_VNIL: return &absentkey;
    case LUA_VNUMFLT: {
      lua_Integer k;
      if (luaV_flttointeger(fltvalue(key), &k, F2Ieq)) /* integral index? */
        return luaH_getint(t, k);  /* use specialized version */
      /* else... */
    }  /* FALLTHROUGH */
    default:
    {
      if l_unlikely(table_cap(t) == 0) return &absentkey;
      if (table_isarray(t)) return &absentkey;
      return map_getgeneric(t, key);
    }
  }
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
    if ( 0 < key && key <= t->arr_count){
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

/*
** nums[i] = number of keys 'k' where 2^(i - 1) < k <= 2^i
*/
static void rehash (lua_State *L, Table *t, const TValue *ek) {
  luaH_addsize(L, t, 1);
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
      luaG_runerror(L, "table index is NaN");// NaN 比较逻辑混乱。
  }
  Node* mp = NULL;
  if (table_cap(t) > 0) {
    mp = mainpositionTV(t, key);
  }
  if (mp == NULL || !isempty(gval(mp))) {  /* main position is taken? */
    Node *othern;
    Node *f = map_getfreepos(t);  /* get a free place */
    if (f == NULL) {  /* cannot find a free place? */
      rehash(L, t, key);  /* grow table */
      /* whatever called 'newkey' takes care of TM cache */
      luaH_set(L, t, key, value);  /* insert key into grown table */
      return;
    }
    lua_assert(mp != NULL);
    othern = mainpositionfromnode(t, mp);
    if (othern != mp) {  /* is colliding node out of its main position? */
      /* yes; move colliding node into free position */
      while (othern + gnext(othern) != mp)  /* find previous */
        othern += gnext(othern);
      gnext(othern) = cast_int(f - othern);  /* rechain to point to 'f' */
      *f = *mp;  /* copy colliding node into free pos. (mp->next also goes) */
      if (gnext(mp) != 0) {
        gnext(f) += cast_int(mp - f);  /* correct 'next' */
        gnext(mp) = 0;  /* now 'mp' is free */
      }
      setnilvalue(gval(mp));
    }
    else {  /* colliding node is in its own main position */
      /* new node will go into free position */
      if (gnext(mp) != 0)
        gnext(f) = cast_int((mp + gnext(mp)) - f);  /* chain new position */
      else lua_assert(gnext(f) == 0);
      gnext(mp) = cast_int(f - mp);
      mp = f;
    }
  }
  setnodekey(L, mp, key);
  luaC_barrierback(L, obj2gco(t), key);
  lua_assert(isempty(gval(mp)));
  setobj2t(L, gval(mp), value);
  map_add_one(t, mp);
}

void luaH_newarrayitem (lua_State *L, Table *t, lua_Integer idx, TValue *value) {
  lua_assert(table_isarray(t));
  // 做了极大的限制，array只能一个一个的加
  int32_t newcount = (int)idx;
  if l_unlikely(t->arr_count+1 != newcount) {
    luaG_runerror(L, "array can only add at last, may use table.resize before.");
    return;
  }
  // grow array
  if (t->data == NULL || newcount > twoto(t->lsizenode)){
    luaH_addsize(L, t, 1);
  }
  t->arr_count = newcount;
  setobj2array(L, get_array_val(t, newcount-1), value);
}

/* mark an entry as empty. only luaH_remove can use*/
// #define setempty(v)		settt_(v, LUA_VEMPTY)

void luaH_remove (Table *t, Node *node) {
  if (!isempty(gval(node))){
    setnilvalue(gval(node));
    map_del_one(t, node);
  }
}

void luaH_clearkey (Node *n) {
  lua_assert(isempty(gval(n)));
  if (keyiscollectable(n)){
    TValue key;
    getnodekey(cast(lua_State *, NULL), &key, n);
    deadkeyhash(n) = gethash(&key);
    setdeadkey(n);  /* unused key; remove it */
  }
}

lua_Unsigned luaH_getn (Table *t) {
  return table_isarray(t) ? array_count(t) : map_getcount(t);
}