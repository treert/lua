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
--[[
  想仿照 Python 的可变参数 *args 和 可变命名参数 **kwargs，让mylua支持命名参数和可变命名参数。
	经过一番尝试决定实现个简化版的：调用方可以使用 f(1, 2, a=1, *args, c=2, 4, 5) 传递命名参数。
	1. *args 和 k=v 的平级的，谁在后面，谁优先级高，支持多组。
		- 只会把 args 当成 map 使用 rawget 去读。（不会报任何错误）。
	2. k=nil 可以传递，*{k=nil} 就不行了，原因是 nil没法作为 map的值。
	3. 用命名参数去调用普通 c 函数，会报错。【调用方需要明确知道自己调用的函数是lua函数】、
		- 如果需要，用 lua 函数包装一下 c 函数

	1. 函数定义时为什么没有用 *kwargs 去接收额外的命名参数呢？
	  - 具体实现时发现会和 lua 的 ... 机制发生干扰，不好实现，实现了使用起来也不方便。
		- 不利于 IDE 提示，特别是回调函数的情况。命名参数只支持直接的一层，调用方需要明确知道自己调用的函数的参数名。
			- 如果真的需要传递额外的参数，用 table 传参数。
	2. 为什么又想起来支持 *args 了。
		- 这样方便在调用函数前，组装好 table，然后调用，编写代码时确实有好处。

]]

-- 技巧：命名参数可以分割普通参数和 ... , 它之前的不会算入 ... , 算是个附带效果。 
f(11, a=1, c=2, 11, 22, 33)
-- 支持特殊的 *=table
f(11, a=1, *{b=2,c=3}, 22, 33)

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