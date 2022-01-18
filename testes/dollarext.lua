
local s = "123"
assert("123 nil" == $"$s $sss")

assert("1+2=3" == $"1+2=${1+2}")

assert("2+3=5" == "2" ..$"+${3}"..$"=${2+3}")

print "$string ok"

local f1 = ${ return "f1" }

local f2 = $(ff){return "f2 " .. ff()}

assert(f2(f1) == "f2 f1")

print "$function ok"
