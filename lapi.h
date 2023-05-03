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
由于GC和异常机制的问题。api类的函数需要确保不遇到问题。
如果能确保函数执行的代码全部是c的，其实可以访问内部结构的。
如果函数虽然是c的，但是又直接或间接调用到 error 类的函数，需要确保 new出的对象放到栈上了。
这个可以参考 luaL_Buffer 的使用。理论上来说，应该都用这个结构的。
-------------------------------------------------
lua 没有暴露 TValue 之类的给 外部，甚至都不暴露给内置的库。
比如 tablib.c 的实现完全没有用到内部结构信息。非常低效呀。内部库不应该可以访问内部结构吗？
*/
LUAI_FUNC TValue* luaA_index2value(lua_State *L, int idx);

LUAI_FUNC void luaA_pushvalue(lua_State *L, TValue* v);

#endif
