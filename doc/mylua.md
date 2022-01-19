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