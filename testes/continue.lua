local a = 1
for i = 1, 10, 1 do
    if i%2 == 0 then
        a = a+i
    end
end
local b = 1
for i = 1, 10, 1 do
    if i%2 ~= 0 then
        continue
    end
    b = b+i
end

assert(a == b)
print "fornum ok"

b = 1
local foritor = function (count)
    return function (tb,idx)
        if idx >= count then
            return nil
        else
            return idx + 1
        end
    end,nil,0
end
for i in foritor(10) do
    print("i="..i)
    if i%2 ~= 0 then
        continue
    end
    print("ii="..i)
    b = b+i
end



assert(a == b)
print "forin ok"

do return end

local i = 1
local c = 1
while i <= 10 do
    if i%2 ~= 0 then
        i = i+1
        continue
    end
    c = c+i
    i = i+1
end

assert(a == c)

print "while ok"

i = 1
c = 1
repeat
    if i%2 ~= 0 then 
        i = i + 1
        continue
    end
    c = c+i
    i = i+1
until i > 10

assert(a == c)

print "repeat ok"



