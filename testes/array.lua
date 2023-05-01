print "start test array"

assert(#[] == 0)
assert(#[1,nil,3] == 3)
assert(#[1,nil,nil] == 3)
assert(#[1,2,3] == 3)

local arr = [1,2]
local ok,msg = pcall(function ()
    arr[101] = 1
end)
assert(ok and #arr == 101)
ok,msg = pcall(function ()
    arr[-1] = 1
end)
print(msg)
assert(not ok and string.find(msg, "array"))
ok,msg = pcall(function ()
    arr[1.2] = 1
end)
print(msg)
assert(not ok and string.find(msg, "array"))
ok,msg = pcall(function ()
    arr[{}] = 1
end)
print(msg)
assert(not ok and string.find(msg, "array"))
ok,msg = pcall(function ()
    -- doc@om 要非常小心呐，2^30 目前不会报错。但是吃掉了 16G 的内存
    arr[2^30+1] = 1
end)
print(msg)
assert(not ok and string.find(msg, "array"))


print "end test array"