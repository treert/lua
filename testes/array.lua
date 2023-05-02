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

local a = table.newmap(100)
assert(#a == 0 and table.get_capacity(a) >= 100)
local a = table.newarray(100)
assert(#a == 0 and table.get_capacity(a) >= 100)

local idx,k,v
local a = [11,nil,33]
idx,k,v = table.next(a, idx)
assert(idx == 1 and k == 1 and v == 11)
idx,k,v = table.next(a, idx)
assert(idx == 3 and k == 3 and v == 33)
idx,k,v = table.next(a, idx)
assert(idx == nil and k == nil and v == nil)

local idx,k,v
local a = {a1=11,a2=nil,a3=33}
idx,k,v = table.next(a, idx)
assert(idx == 1 and k == 'a1' and v == 11)
idx,k,v = table.next(a, idx)
assert(idx == 2 and k == 'a3' and v == 33)
idx,k,v = table.next(a, idx)
assert(idx == nil and k == nil and v == nil)

local m = {
    m = 'mm'
}
local t = setmetatable([],{
    __index = m,
    __newindex = function (ra,k,v)
        local kk = tonumber(k)
        if kk and kk == k and kk > 0 and kk <= (1<<20) then
            rawset(ra, kk, v)
        else
            m[k] = v
        end
    end,
})

t[1] = 11
t[-1] = -11
t.x = 'xx'
t.y = 'yy'
assert(t.x == 'xx')
assert(t.y == 'yy')
assert(t[-1] == -11)
assert(t[1] == 11)
assert(t[0] == nil)
assert(t.m == 'mm')

print "end test array"