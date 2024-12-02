my lua
--------
修改了一点lua的功能和语法。基本还是兼容lua的。
# 不兼容修改
## __concat 废弃了
`$string`的实现复用了`OP_CONCAT`指令，修改了 concat 的实现。
原来的元表项`__concat`就没用了。
现在的实现等价于 `concat(a,b) = tostring(a)..tostring(b)`


# 增量修改，兼容性很高
## continue
增加了 continue 关键字。
在 repeat until 里基本不能使用，除非把局部变量全部定义在开头或者block里。

## $string $function
增加了一批 `$string $function` 的语法糖

```bnf
$string ::= '$' String
$function : '$' [ '(' paramlist ')' ] '{' stats '}'
exp ::= $string | $function

# 额外支持下
funcargs ::= $string | $function
```

## ?? 
三目运算符?:加失败了，因为:在lua里是特殊的符号。

这个语法想了想，还是加上了。主要是为了替换`or`来设置默认值。
```lua
p = p or true -- 这个代码是隐患的，p 逻辑上永远都是true，or 设置默认值没法处理。
```
注意：`a and b or c`遇到要区分 bool 和 nil 时也抓瞎，改成`a and b ?? c`后，可以处理`b = false`的情况，但是`b = nil`时也完蛋。


实现：和 and or 的实现逻辑是不一样的。
and or 的实现也可以用我想的方法，似乎更简单一点，也很容易优化，并且不用维护一个老长的逻辑链条。

## keyword 特殊情况下可以当做普通的name
```lua
local tb = {
	local = 1,
}
tb.end = 1
function tb:for()
	
end

print "tb.end = ok"
```

## 函数调用和函数定义时参数后面可以增加个一个,
```lua
-- 支持
function f(a,b,) end
f(1,2,)
```

## 命名参数支持
```lua
function f(t1,a,b,c,...) end

f(11, a=1,c=2,11,22,33) -- 命名参数可以分割普通参数和 ... , 它之前的不会算入 ...

-- 说明：调用普通c函数难以支持。【这种想了想，就用lua包一层好了。普通c函数不支持命名参数。】

----------------------------------------------------------
--[[
希望像python一样支持`*args`，构建一个`table`，把命名参数放进去。
考虑了一番实现成本，感觉收益不大，放弃了。
1. 函数定义和函数调用的指令是分开的。只能用 ci 来传输标记，同时需要考虑到gc的影响。
2. 或者可以强制每次调用函数都处理命名参数。这样有些浪费。【这种做法，外部c调用lua函数也需要把args当成一个普通参数，传nil或者table】
]]
--[[
function f(a,b,c,d,*args,...) end
f(b=2,b=3, 11) -- 调用时，命名参数要在最签名。虽然它们的优先级高于后面的。
f(*{b=2,c=3}, a=1, 11, 22) -- *tb 优先级最高，要放在k-v之前
f(*{b=2},*{c=3}, a=1, 11, 22)
]]
```

## array 
- 增加 array， 作为 table 变种存在。例子：`[1,2,3]`
  - 语法 `array := '[' exp { seq exp} [seq] ']'`
  - array 不支持 weakmode.
  - `#array` => 曾经写过的最大索引。

## 数字常量支持 '_' 分格
可以支持
```lua
a = 1_000_000
a = 0.000_000_000_01
a = 0x00_14_22_01_23_45
```