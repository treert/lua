[my lua](./doc/mylua.md)
--------
## todo
- [ ] 优化 table.sort, 添加 table.keysort array.sort。【想利用内部结构优化】

## 增量修改，兼容性很高
- 增加了 continue 关键字。
- 增加了一批 `$string $function` 的语法糖
- 增加 ?? 运算。
  - 三目运算符 ?: 加失败了，因为 : 在lua里是特殊的符号。
- keyword 特殊情况下可以当做普通的 name。如：`t.end = 1`
- 函数参数支持命名参数。如：`f(1,2,3,a1=1,a2=2)`
- 增加 array， 作为 table 变种存在。例子：`[1,2,3]`
  - 语法 `array := '[' exp { seq exp} [seq] ']'`
  - array 不支持 weakmode.
  - `#array` => 曾经写过的最大索引。
- 很小的修改
  - 函数调用和函数定义时参数后面可以增加个一个`,`

## 不兼容修改
- __concat 废弃了。`$string`的实现复用了`OP_CONCAT`指令，修改了 concat 的实现。
- 替换掉 table 的实现。现在是纯粹的HashTable了，统一叫做**map**。也增加了**array**数组变种。
  - `#map` => HashTableCount, 
  - 当前的实现方案，table分配的内存只增不减。**`array[2^30]=1`这个代码会分配16G内存！！！**
  - 通过 array + __index __newindex + map, 可以模拟一个 lua 的 table. 不过性能就堪忧了。
- 一些被修改了的 API
  - table.pack 返回的时 Array 了

## 关于table的修改
mylua 把 table 分成了纯粹的 map,array 两个结构。默认是 map。
提供一些API
- `table.newarray(int size)` 可以指定初始容量。最简单的构造是`[1,2,3,4,5]`
- `table.newmap(int size)`
- `table.get_capacity(int size)` 可以容纳的最多的元素个数。可以换算出消耗的内存
- `table.next(t,idx?)` return next_idx,k,v or nil when nomore element.
  - 快速遍历。idx start from 0, 但是应该不用关注。初始传 nil 就行

使用类似 dotnet 的 Dictionary 实现方案。功能上更符合我的需要。
- 语义更加明确，不会再有 `#t` 的争执。
- 方便后续优化`table.sort`之类。
- **实现附带的效果：添加的顺序和遍历的顺序一致。**
  - 如果有空洞，添加的元素先进空洞，顺序就靠前了。
  - 默认初始化`{}`如果数组和kv混合，一般是kv在前。但是不保证。

性能上有利有弊。测试 [benchmark.lua](./testes/benchmark.lua) 结果如下：
| lua5.4 | mylua  | my/lua  |desc (这里的array还是table,不是mylua的array)
| -----  | -----  | --------|-
|1.852   |2.465   |1.33     | Standard (solid)
|2.254   |3.069   |1.36     | Standard (metatable)
|1.907   |2.563   |1.34     | Object using closures (PiL 16.4)
|1.806   |1.75    |0.97     | Object using closures (noself)
|0.853   |1.213   |1.42     | Direct Access
|0.296   |0.305   |1.03     | Local Variable
|3.604   |3.673   |1.02     | table init
|3.823   |2.043   |0.53     | table forin_and_set
|9.241   |8.589   |0.93     | array[2] init
|1.542   |1.929   |1.25     | array[2] reset
|4.369   |2.423   |0.55     | array[2] forin
|6.317   |6.049   |0.96     | array[4] init
|1.035   |1.342   |1.30     | array[4] reset
|2.904   |1.439   |0.50     | array[4] forin
|2.943   |3.451   |1.17     | array[20] init
|0.623   |0.8     |1.28     | array[20] reset
|1.834   |0.502   |0.27     | array[20] forin
|1.261   |1.97    |1.56     | array[200] init
|0.554   |0.735   |1.33     | array[200] reset
|1.55    |0.308   |0.20     | array[200] forin
|1.059   |1.744   |1.65     | array[2000] init
|0.45    |0.678   |1.51     | array[2000] reset
|1.521   |0.287   |0.19     | array[2000] forin

### 旧的测试数据，包含了 Array 的测试.
[test-performance](./testes/test-performance.lua) 旧的测试代码. 里面包含的不兼容lua的代码。Array 和 lua 纯粹使用数组差不多。
```lua
--[[
测试规模：table_size=1000_000 for_loop=100。PS: lua没有array
测试耗时(mylua/lua)：
    for in                   => 20%
    init map                 => 210%
    init array / lua map     => 110%

具体数据：cmake --config Relese：
mylua:
int array cost 0.011s        0.009s (如果用 table.newarray(size) 初始化)
for array cost 0.270s
init map cost 0.021s         0.016s (如果用 table.newmap(size) 初始化)
for map cost 0.290s
lua:
init map cost 0.010s
for map cost 1.486s
]]
```
# Lua Official

This is the repository of Lua development code, as seen by the Lua team. It contains the full history of all commits but is mirrored irregularly. For complete information about Lua, visit [Lua.org](https://www.lua.org/).

Please **do not** send pull requests. To report issues, post a message to the [Lua mailing list](https://www.lua.org/lua-l.html).

Download official Lua releases from [Lua.org](https://www.lua.org/download.html).
