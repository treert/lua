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

?? 也加失败了，短路逻辑比想象的要复杂呀。

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