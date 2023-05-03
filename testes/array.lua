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


print("test stable_sort")
local a = [1,nil,3,2]
table.stable_sort(a)
assert(a[2] == 2 and a[4] == nil and #a == 4)
table.shrink(a)
assert(a[2] == 2 and #a == 3)

local a = {a1=1,a22=2,a333=333, 11,22,-33}
table.stable_sort(a)
local idx,k,v = table.next(a)
assert(idx == 1 and k == 3 and v == -33)

table.stable_sort(a, function (k,v)
    return -v
end, 3)
local idx,k,v = table.next(a)
assert(idx == 1 and k == 'a333' and v == 333)

table.stable_sort(a, function (k,v)
    return -v
end, -3)
local idx,k,v = table.next(a)
assert(idx == 1 and k == 3 and v == -33)

pcall(function ()
    table.stable_sort(a, function (k,v)
        error("hahaha")
        return -v
    end, 3)
end)
local idx,k,v = table.next(a)
assert(idx == 1 and k == 3 and v == -33) -- 有错误保持不变

-- 测试下gc 【应该UBOX分配的内存数量时自己管理的没通知lua，lua根本不知道】
print("start test stable_sort memory cost")
local size = 2^5
local onek = 2^10
collectgarbage()
local mm1 = collectgarbage("count")
local a = []
a[size*onek] = 1
local mm2 = collectgarbage("count")
local mmt1,mmt2
do 
    table.stable_sort(a) -- 会临时增加内存。但是感知不到
    mmt1 = collectgarbage("count") 
    assert(mmt1 - mm2 < size)
end
do
    local ok,msg = pcall(function ()
        table.stable_sort(a, function ()
            mmt1 = collectgarbage("count")
            print("stable_sort mm", mm1, mm2, mmt1) -- 这儿也感知不到
            error("okmsg")
        end)
    end)
    print(msg)
    assert(string.find(msg, 'okmsg'))
    mmt1 = collectgarbage("count") -- 也感知不到临时内存
    assert(mmt1 - mm2 < size)
end
local mmt2 = collectgarbage("count")
print("mmt point", mm1, mm2, mmt1, mmt2)
a = nil
collectgarbage()
local mm3 = collectgarbage("count")
print("mm point", mm1, mm2, mm3)
assert(mm2 - mm1 > size and mm2 - mm3 > size)

local mm1 = collectgarbage("count")
local a = []
a[size*onek] = 1
table.stable_sort(a)
local mm2 = collectgarbage("count")
table.shrink(a)
local mm3 = collectgarbage("count")
print("shrink table mm point", mm1, mm2, mm3)
assert(mm2 - mm1 > size and mm2 - mm3 > size)

print("test t[] = 1")
local a = {}
a[] = 1
a[] = 2
assert(a[1] == 1 and a[2] == 2)
local func,msg = load("print(a[])") -- 这个是语义级别的错误。用pcall是不行的。编译就报错了
assert(func == nil and string.find(msg, "t[]",1,1))

print "end test array"