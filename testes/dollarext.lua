print "$ Start\n"

local s = "123"
assert("123 nil" == $"$s $sss")

assert("1+2=3" == $"1+2=${1+2}")

assert("2+3=5" == "2" ..$"+${3}"..$"=${2+3}")

print "$string ok"

local f1 = ${ return "f1" }

local f2 = $(ff){return "f2 " .. ff()}

assert(f2(f1) == "f2 f1")

print "$function ok"

local tb = {
	local = 1,
}
tb.end = 1
function tb:for()
	
end

print "tb.end = ok"

local xxxx = nil
assert((xxxx??123) == 123)
assert((xxxx??xxxx??false or 123) == 123)
assert((false??123) == false)
assert((false or 123) == 123)

print "$ OK\n"