[my lua](./doc/mylua.md)
--------
## todo
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
  - 现在`#t`可以快速获得table大小，是hashtable里实际元素的个数。
- 增加 Array， 作为table变种存在。实现的非常简单。
- 一些被修改了的 API
  - table.pack 不再填充 n 字段了。

## 性能
mylua 优化了table的遍历。【sort之类的也准备优化下】
[test-performance.lua](./testes/test-performance.lua)

```lua
--[[
测试规模：table_size=1000_000 for_loop=100。PS: lua没有array
测试耗时(mylua/lua)：
    for in                   => 20%
    init map                 => 210%(300% Debug)
    init array / lua map     => 110%(130% Debug)

具体数据：
cmake --config Debug:
mylua:
int array cost 0.024s
for array cost 0.658s
init map cost 0.055s
for map cost 0.683s
lua:
init map cost 0.018s
for map cost 3.799s

cmake --config Relese：
mylua:
int array cost 0.011s
for array cost 0.274s
init map cost 0.021s
for map cost 0.287s
lua:
init map cost 0.010s
for map cost 1.486s
]]

-- 可以lua运行的测试代码。默认的size和loop太小了，没什么区分度。
local size,loop = tonumber(arg[1]) or 1000,tonumber(arg[2]) or 1000

local t1 = os.clock()
local tt = {}
for i = 1, size do
    tt[i] = i
end
print(string.format("init map cost %.3fs",os.clock()-t1))
assert(#tt == size)

local count = 0
local t1 = os.clock()
for i = 1, loop do
    for k,v in pairs(tt) do
        -- count = count + k + v
    end
end
print(string.format("for map cost %.3fs",os.clock()-t1))
-- assert(count == loop*(size + 1)*size)

print("start end")
```

# Lua Official

This is the repository of Lua development code, as seen by the Lua team. It contains the full history of all commits but is mirrored irregularly. For complete information about Lua, visit [Lua.org](https://www.lua.org/).

Please **do not** send pull requests. To report issues, post a message to the [Lua mailing list](https://www.lua.org/lua-l.html).

Download official Lua releases from [Lua.org](https://www.lua.org/download.html).
