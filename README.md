[my lua](./doc/mylua.md)
--------
## todo
- [ ] 实现 Array。【现在的想法，Array作为table变种存在。实现一个非常简单的】
- [ ] 优化 table.sort, 添加 table.keysort array.sort。【lua原先的实现能支持】

## 增量修改，兼容性很高
- 增加了 continue 关键字。
- 增加了一批 `$string $function` 的语法糖
- 增加 ?? 运算。
  - 三目运算符 ?: 加失败了，因为 : 在lua里是特殊的符号。
- keyword 特殊情况下可以当做普通的name
- 函数参数支持命名参数。
- 很小的修改
  - 函数调用和函数定义时参数后面可以增加个一个,

## 不兼容修改
- __concat 废弃了。`$string`的实现复用了`OP_CONCAT`指令，修改了 concat 的实现。
- 替换掉 Table 的实现。现在table是纯粹的HashTable了。
- 一些被修改了的 API
  - table.pack 不再填充 n 字段了。现在`#t`可以快速获得table大小，是hashtable里实际元素的个数。

# Lua Official

This is the repository of Lua development code, as seen by the Lua team. It contains the full history of all commits but is mirrored irregularly. For complete information about Lua, visit [Lua.org](https://www.lua.org/).

Please **do not** send pull requests. To report issues, post a message to the [Lua mailing list](https://www.lua.org/lua-l.html).

Download official Lua releases from [Lua.org](https://www.lua.org/download.html).
