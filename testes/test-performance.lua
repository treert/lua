
local size,loop = tonumber(arg[1]) or 1000,tonumber(arg[2]) or 1000
print($"start test performance, size=$size loop=$loop")

local t1 = os.clock()
local tt = {}
for i = 1, size do
    tt[i] = i
end
print(string.format("init map cost %.3fs",os.clock()-t1))
assert(#tt == size)

local count = 0
local t1 = os.clock()
for i = 1, loop do
    for k,v in pairs(tt) do
        -- count = count + k + v
    end
end
print(string.format("for map cost %.3fs",os.clock()-t1))
-- assert(count == loop*(size + 1)*size)

print("start end")