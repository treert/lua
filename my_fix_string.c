#define LUA_LIB
#include "my_fix_string.h"
#include "stdlib.h"
#include "string.h"
#include "memory.h"


typedef struct MyFixStringHead
{
  struct MyFixStringHead* next;
  int32_t len;
  int32_t hash;
} MyFixStringHead;

#define _my_head_sz sizeof(MyFixStringHead)
#define _my_sz_node(len) (_my_head_sz * (2 + len/_my_head_sz))
#define _my_str(head) ((const char*)(head + 1))

static uint8_t* _my_fixstring_pool = NULL;
static uint8_t* _my_fixstring_pool_end = NULL;

static MyFixStringHead** _my_fixstring_hash = NULL;
#define _my_check_init() { if (_my_fixstring_hash != NULL) {\
 abort(); /* todo@om trigger error */\
 } }

static void *_my_alloc (void *ptr, size_t nsize) {
  void*p = realloc(ptr, nsize);
  if (p == NULL) abort();
  return p;
}

static const uint32_t _my_primes[ 31] = {
    1,3,5,11,19,41,79,157,317,631,1259,2521,5039,10079,20161,40343,80611,161221,322459,644881,1289749,2579513,5158999,10317991,20635981,41271991,82543913,165087817,330175613,660351239,1320702451
};

static uint32_t _my_hashsize = 0;
static uint64_t _my_fastmod_multiplier = 0;

inline static MyFixStringHead** my_getbucket(uint32_t hash){
    uint32_t highbits = (uint32_t)(((((_my_fastmod_multiplier * hash) >> 32) + 1) * _my_hashsize) >> 32);
    return _my_fixstring_hash + highbits;
}

inline static void _my_choose_hash_size(int num) {
  int lsize = 0;
  while ((1<<lsize) >= num) break;
  _my_hashsize = _my_primes[lsize];
  _my_fastmod_multiplier = UINT64_MAX/_my_hashsize + 1;
}

static const _my_seed = 0;

MY_API uint32_t my_fixstring_hash(const char* str, size_t len) {
  // 来自 lua 的实现，hash37
  uint32_t h = _my_seed ^ (uint32_t)(len);
  for (; len > 0; len--)
    h ^= ((h<<5) + (h>>2) + (uint8_t)(str[len - 1]));
  return h;
}


MY_API int my_fixstring_init(const char** strs){
  _my_check_init();
  size_t pool_size = 0;
  int num = 0;
  const char** head = strs;
  while ( head != NULL)
  {
    const char* str = *head;
    size_t len = strlen(str);
    num ++;
    pool_size += _my_sz_node(len);
    head ++;
  }
  _my_choose_hash_size(num);
  _my_fixstring_hash = (MyFixStringHead**)_my_alloc(NULL, sizeof(MyFixStringHead*)*_my_hashsize);
  _my_fixstring_pool = (uint8_t*)_my_alloc(NULL, pool_size);
  _my_fixstring_pool_end = _my_fixstring_pool + pool_size;
  // build hash
  memset(_my_fixstring_hash, 0, sizeof(MyFixStringHead*)*_my_hashsize);
  head = strs;
  num = 0;
  MyFixStringHead* cur_node = (MyFixStringHead*)_my_fixstring_pool;
  while ( head != NULL)
  {
    const char* str = *head;
    size_t len = strlen(str);
    cur_node->len = (int32_t)len;
    cur_node->hash = my_fixstring_hash(str, len);
    memcpy(cur_node+1, str, len);
    MyFixStringHead** bucket = my_getbucket(cur_node->hash);
    cur_node->next = *bucket;
    *bucket = cur_node;
    cur_node = (MyFixStringHead*)(((uint8_t*)cur_node) + _my_sz_node(len));
    num ++;
    head ++;
  }
  return 1;
}

MY_API const char* my_fixstring_check(const char* str, size_t len){
  if (_my_fixstring_pool == NULL) return NULL;// todo@om error

  if (str < _my_fixstring_pool_end && str > _my_fixstring_pool) {
    return str;// pool 内的，保持不变
  }
  uint32_t hash = my_fixstring_hash(str, len);
  MyFixStringHead* head = *my_getbucket(hash);
  while (head != NULL)
  {
    if (memcmp(_my_str(head), str, len) == 0) {
      return _my_str(head);
    }
    head = head->next;
  }
  return NULL;
}