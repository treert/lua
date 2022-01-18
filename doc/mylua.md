my lua
--------
修改了一点lua的功能和语法。基本还是兼容lua的。

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