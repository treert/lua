/*
** $Id: lapi.h $
** Auxiliary functions from Lua API
** See Copyright Notice in lua.h
*/

#ifndef lapi_h
#define lapi_h


#include "llimits.h"
#include "lstate.h"


/* Increments 'L->top', checking for stack overflows */
#define api_incr_top(L)   {L->top++; api_check(L, L->top <= L->ci->top, \
				"stack overflow");}

// mylua 增加的版本
#define api_incr_top_n(L,n)   {L->top+=(n); api_check(L, L->top <= L->ci->top, \
				"stack overflow");}


/*
** If a call returns too many multiple returns, the callee may not have
** stack space to accommodate all results. In this case, this macro
** increases its stack space ('L->ci->top').
*/
#define adjustresults(L,nres) \
    { if ((nres) <= LUA_MULTRET && L->ci->top < L->top) L->ci->top = L->top; }


/* Ensure the stack has at least 'n' elements */
#define api_checknelems(L,n)	api_check(L, (n) < (L->top - L->ci->func), \
				  "not enough elements in the stack")


/*
** To reduce the overhead of returning from C functions, the presence of
** to-be-closed variables in these functions is coded in the CallInfo's
** field 'nresults', in a way that functions with no to-be-closed variables
** with zero, one, or "all" wanted results have no overhead. Functions
** with other number of wanted results, as well as functions with
** variables to be closed, have an extra check.
*/

#define hastocloseCfunc(n)	((n) < LUA_MULTRET)

/* Map [-1, inf) (range of 'nresults') into (-inf, -2] */
#define codeNresults(n)		(-(n) - 3)
#define decodeNresults(n)	(-(n) - 3)

/* doc@om
lua 没有暴露 TValue 之类的给 外部，甚至都不暴露给内置的库。
比如 tablib.c 的实现完全没有用到内部结构信息。非常低效呀。内部库不应该可以访问内部结构吗？
*/
LUAI_FUNC TValue* luaA_index2value(lua_State *L, int idx);

#endif
