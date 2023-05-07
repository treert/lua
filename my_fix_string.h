/**
 * @file my_fix_string.h
 * @author onemore (909413016@qq.com)
 * @brief 
 * @version 0.1
 * @date 2023-05-07
 * 
 * 
 */

/*
Const String Pool.
主要目的是想将字符串指针 char* 当初 int64类型的字面值来使用。
1. 相等判断非常简单，比较指针就可以。
2. 方便跨线程共享使用。（字符串池先初始化，然后常驻内存。）




其他使用说明：
- 可以直接用指针来hash，也可以选择内部的hash值。
-【通过排序字符串，也可以比较字符串大小。如果有需要再加上】

Some Detail:
1. PreInit and Once Init Read everywhere. **Do Not Modify!**.
2. string is store sequence. so can easy check is in pool.
 */
#ifndef MY_FIX_STRING_H
#define MY_FIX_STRING_H

#include "stdint.h"
#include "lua.h"

#define MY_API LUA_API

MY_API uint32_t my_fixstring_hash(const char* str, size_t len);

MY_API int my_fixstring_init(const char** strs);

MY_API const char* my_fixstring_check(const char* str, size_t len);



#ifdef __cplusplus
}
#endif

#endif /* MY_FIX_STRING_H */