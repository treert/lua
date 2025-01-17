print "test named args Start"

local function f(a,b,c)
	return $"$a $b $c"
end

assert("1 2 3" == f(1,2,3))
-- print(f(a=1,c=3,b=2))
-- assert("1 2 3" == f(a=1,c=3,b=2))
assert("1 2 3" == f(1,c=3,b=2))
print("ok 11")

-- 和可变参数额支持有些冲突哎。现在的实现差那么点。python这方面就非常棒。

local function g()
	return 1,2,3
end

local function f(a,...)
	for i,v in ipairs{...} do
		a = a + v
	end
	return a
end

assert(f(1,2,3) == 6)
assert(f(g()) == 6)
assert(f(a=11, g()) == 11 + 6) 
assert(f(a=11,b=11, g()) == 11 + 6) 

print "test named args OK"