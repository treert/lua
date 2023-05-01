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
// lua float
inline static int32_t gethash_double(double number){
  int64_t num = *(int64_t*)(&number);
	if (((num - 1) & 0x7FFFFFFFFFFFFFFFL) >= 9218868437227405312L)
	{
		num &= 0x7FF0000000000000L;
	}
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
    case LUA_VLIGHTUSERDATA: {
      void *p = pvalue(key);
      return gethash_ptr(p);
    }
    case LUA_VLCF: {
      lua_CFunction f = fvalue(key);
      return gethash_ptr(f);
    }
    default: {
      GCObject *o = gcvalue(key);
      return gethash_ptr(o);
    }
  }
}

/// Size And Prime
static const uint32_t s_primes[MAX_LOG_SIZE + 1] = {
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

// 通过 hash 值计算 bucket index
inline static uint32_t getbucketidx(uint32_t hash, uint8_t log_size)
{
  lua_assert(log_size <= MAX_LOG_SIZE);
  uint32_t d = s_primes[log_size];
  uint64_t m = s_primes_fastmod_multiplier[log_size];
  return helper_FastMod(hash, d, m);
}

#define gettableindexmemsize(lsize) (sizeof(int32_t)*s_primes[lsize])
#define gettablememsize(lsize) (sizeof(Node)*twoto(lsize) + gettableindexmemsize(lsize))

// get bucket index start ptr. bucket index store after node
#define getbucketstart(t)       ((int32_t*)(t->node + twoto(t->lsizenode)))
// get bucket head.
#define getbucket(t,idx)        (getbucketstart(t) + (idx))
#define getbucketbyhash(t,hash) (getbucketstart(t) + getbucketidx(hash,t->lsizenode))


#define dummynode		(&dummynode_)

static const Node dummynode_ = {
  {{NULL}, LUA_VEMPTY,  /* value's value and type */
   LUA_VNIL, 0, {NULL}}  /* key type, next, and key value */
};


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
    case LUA_VLIGHTUSERDATA:
      return pvalue(k1) == pvalueraw(keyval(n2));
    case LUA_VLCF:
      return fvalue(k1) == fvalueraw(keyval(n2));
    case ctb(LUA_VLNGSTR):
      return luaS_eqlngstr(tsvalue(k1), keystrval(n2));
    default:
      return gcvalue(k1) == gcvalueraw(keyval(n2));
  }
}

/*
** Search function for integers.
*/
static Node* getint_node (Table *t, lua_Integer key) {
  lua_assert(t->node != NULL);
  Node* nodes = t->node;
  uint32_t hash = gethash_int64(key);
  int32_t* bucket = getbucketbyhash(t, hash);
  int32_t nx = *bucket;
  while(nx >= 0){
    Node* n = nodes + nx;
    if (keyisinteger(n) && keyival(n) == key){
      return n;
    }
    else{
      nx = gnext(n);
    }
  }
  return NULL;
}


/*
** search function for short strings
*/
static Node* getshortstr_node (Table *t, TString *key) {
  lua_assert(t->node != NULL);
  Node* nodes = t->node;
  uint32_t hash = key->hash;
  int* bucket = getbucketbyhash(t, hash);
  int nx = *bucket;
  while(nx >= 0){
    Node* n = nodes + nx;
    if (keyisshrstr(n) && eqshrstr(keystrval(n), key)){
      return n;
    }
    else{
      nx = gnext(n);
    }
  }
  return NULL;
}

/*
** "Generic" get version.
*/
static Node* getgeneric_node(Table *t, const TValue *key) {
  lua_assert(t->node != NULL);
  lua_assert(!ttisnil(key));
  Node* nodes = t->node;
  uint32_t hash = gethash(key);
  int32_t* bucket = getbucketbyhash(t, hash);
  int32_t nx = *bucket;
  while(nx >= 0){
    Node* n = nodes + nx;
    if (equalkey(key, n)){
      return n;
    }
    else{
      nx = gnext(n);
    }
  }
  return NULL;
}

int luaH_next (lua_State *L, Table *t, StkId key) {
  Node* node;
  if (!ttisnil(s2v(key))){
    node = getgeneric_node(t, s2v(key));
    if (l_unlikely(node == NULL)){
      luaG_runerror(L, "invalid key to 'next'");  /* key not found */
      return 0;// lua 是会报错的。【todo@om 可以直接注释掉上面的抛错，这样next就不会报错了。只是这样和lua不兼容了 】
    }
    node += 1;
  }
  else{
    if(table_count(t) == 0) return 0;
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

int luaH_itor_next (lua_State *L, Table *t, int32_t itor_idx, StkId ret_idx) {
  lua_assert(itor_idx >= 0);
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

/*
  HashTable 只增加，不减小。【也许以后会改吧。先这么着吧。实现中会有很多好处。】
*/
void luaH_addsize (lua_State *L, Table *t, int32_t addsize){
  if (addsize <= 0) return;
  int32_t oldcount = table_count(t);
  int32_t newcount = oldcount + addsize;
  int lsize = luaO_ceillog2(newcount);
  if(lsize > MAX_LOG_SIZE){
    luaG_runerror(L, "table overflow");
  }
  size_t oldsize = t->node == NULL ? 0: gettablememsize(t->lsizenode);
  size_t newsize = gettablememsize(lsize);
  t->node = (Node*)luaM_realloc(L, t->node, oldsize, newsize);
  t->lsizenode = lsize;
  // rebuild bucket idx
  memset(getbucketstart(t),-1,gettableindexmemsize(t->lsizenode));
  for (int32_t i = 0; i < oldcount; i++) {
    Node *n = gnode(t, i);
    if(!isempty(gval(n))){
      TValue key;
      getnodekey(L, &key, n);
      uint32_t hash = gethash(&key);
      int32_t* bucket = getbucketbyhash(t, hash);
      gnext(n) = *bucket;
      *bucket = i;
    }
  }
}

void luaH_resize (lua_State *L, Table *t, unsigned int newasize, unsigned int nhsize) {
  int count = table_count(t);
  luaH_addsize(L, t, newasize + nhsize - count);
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
  t->node = NULL;  /* lua use common 'dummynode', mylua just set NULL*/
  return t;
}


void luaH_free (lua_State *L, Table *t) {
  if (t->node != NULL)
    luaM_freemem(L, t->node, gettablememsize(t->lsizenode));
  luaM_free(L, t);
}

/*
** Search function for integers.
*/
const TValue *luaH_getint (Table *t, lua_Integer key) {
  if(table_count(t) == 0) return &absentkey;
  Node* n = getint_node(t, key);
  return n ? gval(n) : &absentkey;
}


/*
** search function for short strings
*/
const TValue *luaH_getshortstr (Table *t, TString *key) {
  if(table_count(t) == 0) return &absentkey;
  Node* n = getshortstr_node(t, key);
  return n ? gval(n) : &absentkey;
}

/*
** search function for strings
*/
const TValue *luaH_getstr (Table *t, TString *key) {
  if(table_count(t) == 0) return &absentkey;
  Node* n;
  if (key->tt == LUA_VSHRSTR)
    n = getshortstr_node(t, key);
  else {  /* for long strings, use generic case */
    TValue ko;
    setsvalue(cast(lua_State *, NULL), &ko, key);
    n = getgeneric_node(t, &ko);
  }
  return n ? gval(n) : &absentkey;
}


/*
** main search function
*/
const TValue *luaH_get (Table *t, const TValue *key) {
  if(table_count(t) == 0) return &absentkey;
  Node* n;
  switch (ttypetag(key)) {
    case LUA_VSHRSTR:
      n = getshortstr_node(t, tsvalue(key));
      break;
    case LUA_VNUMINT:
      n = getint_node(t, ivalue(key));
      break;
    case LUA_VNIL: return &absentkey;// throw error?
    case LUA_VNUMFLT: {
      lua_Integer k;
      if (luaV_flttointeger(fltvalue(key), &k, F2Ieq)) /* integral index? */
      {
        n = getint_node(t, k);/* use specialized version */
        break;
      }
      /* else... */
    }  /* FALLTHROUGH */
    default:
      n = getgeneric_node(t, key);
  }
  return n ? gval(n) : &absentkey;
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
  const TValue *slot = luaH_getint(t, key);
  TValue k;
  setivalue(&k, key);
  luaH_finishset(L, t, &k, slot, value);
}

// 添加 (key,value), key should not exsit. maybe need barrier value.
void luaH_newkey (lua_State *L, Table *t, const TValue *key, TValue *value) {
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

  Node* nodes = t->node;
  Node* newnode;
  if (t->freecount > 0) {
    newnode = nodes + t->freelist;
    t->freelist = gnext(newnode);
    t->freecount--;
  }
  else {
    if(t->node == NULL || t->count == twoto(t->lsizenode)){
      // grow hash
      luaH_addsize(L, t, 1);
      nodes = t->node;
    }
    newnode = nodes + t->count;
    t->count++;
  }
  uint32_t hash = gethash(key);
  int32_t* bucket = getbucketbyhash(t, hash);
  gnext(newnode) = *bucket;
  *bucket = (int32_t)(newnode - nodes);
  setnodekey(L, newnode, key);
  luaC_barrierback(L, obj2gco(t), key);
  setobj2t(L, gval(newnode), value);
}

/* mark an entry as empty. only luaH_remove can use*/
#define setempty(v)		settt_(v, LUA_VEMPTY)

void luaH_remove (Table *t, Node *node) {
  lua_assert(t->node != NULL);
  TValue key;
  lua_State *fakeL = NULL;
  getnodekey(fakeL, &key, node);
  uint32_t hash = gethash(&key);
  int32_t* bucket = getbucketbyhash(t, hash);
  Node* nodes = t->node;
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
  setempty(gval(node));// 这边使用的方法是置空 key
  // add to freelist
  t->freecount++;
  gnext(node) = t->freelist;
  t->freelist = nx;
}

lua_Unsigned luaH_getn (Table *t) {
  return table_count(t);
}