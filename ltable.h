/*
** $Id: ltable.h $
** Lua tables (hash)
** See Copyright Notice in lua.h
*/

#ifndef ltable_h
#define ltable_h

#include "lobject.h"

/*
** Clear all bits of fast-access metamethods, which means that the table
** may have any of these metamethods. (First access that fails after the
** clearing will set the bit again.)
*/
#define invalidateTMcache(t)	((t)->flags &= ~maskflags)


LUAI_FUNC const TValue *luaH_getint (Table *t, lua_Integer key);
LUAI_FUNC const TValue *luaH_getshortstr (Table *t, TString *key);
LUAI_FUNC const TValue *luaH_getstr (Table *t, TString *key);
LUAI_FUNC const TValue *luaH_get (Table *t, const TValue *key);

LUAI_FUNC void luaH_set (lua_State *L, Table *t, const TValue *key, TValue *value);
LUAI_FUNC void luaH_setint (lua_State *L, Table *t, lua_Integer key, TValue *value);
LUAI_FUNC void luaH_finishset (lua_State *L, Table *t, const TValue *key, const TValue *slot, TValue *value);

LUAI_FUNC Table *luaH_new (lua_State *L);
LUAI_FUNC Table *luaH_newarray (lua_State *L);

LUAI_FUNC void luaH_newkey (lua_State *L, Table *t, const TValue *key, TValue *value);
LUAI_FUNC void luaH_newarrayitem (lua_State *L, Table *t, lua_Integer idx, TValue *value);
// should only use this func to delete (key,value)
LUAI_FUNC void luaH_remove (Table *t, Node *node);

LUAI_FUNC void luaH_addsize (lua_State *L, Table *t, int32_t addsize);
LUAI_FUNC void luaH_resize (lua_State *L, Table *t, unsigned int nasize, unsigned int nhsize);

LUAI_FUNC void luaH_free (lua_State *L, Table *t);

LUAI_FUNC int luaH_next (lua_State *L, Table *t, StkId key);
// 迭代器。itor_idx start from 0, 返回的下一个 itor_idx，如果 < 0, 迭代结束，没有结果。结果保存到 key:ret_idx,value:ret_idx+1
LUAI_FUNC int luaH_itor_next (lua_State *L, Table *t, int32_t itor_idx, StkId ret_idx);

LUAI_FUNC lua_Unsigned luaH_getn (Table *t);

#endif
