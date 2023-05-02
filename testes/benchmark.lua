
-- http://lua-users.org/wiki/ObjectBenchmarkTests
-- Benchmarking support.
do
    local function runbenchmark(name, code, count, ob)
        local f = load([[
            local count,ob = ...
            local clock = os.clock
            local start = clock()
            for i=1,count do ]] .. code .. [[ end
            return clock() - start
        ]])
        io.write(string.format("%-8.6g| %s\n",f(count, ob), name))
    end

    local nameof = {}
    local codeof = {}
    local tests  = {}
    function addbenchmark(name, code, ob)
        nameof[ob] = name
        codeof[ob] = code
        tests[#tests+1] = ob
    end
    function clearbenchmark()
        tests = {}
        nameof = {}
        codeof = {}
    end
    function runbenchmarks(count)
        for _,ob in ipairs(tests) do
        runbenchmark(nameof[ob], codeof[ob], count, ob)
        end
    end
end
  
function makeob1()
local self = {data = 0}
function self:test()  self.data = self.data + 1  end
return self
end
addbenchmark("Standard (solid)", "ob:test()", makeob1())

local ob2mt = {}
ob2mt.__index = ob2mt
function ob2mt:test()  self.data = self.data + 1  end
function makeob2()
return setmetatable({data = 0}, ob2mt)
end
addbenchmark("Standard (metatable)", "ob:test()", makeob2())

function makeob3() 
local self = {data = 0};
function self.test()  self.data = self.data + 1 end
return self
end
addbenchmark("Object using closures (PiL 16.4)", "ob.test()", makeob3())

function makeob4()
local public = {}
local data = 0
function public.test()  data = data + 1 end
function public.getdata()  return data end
function public.setdata(d)  data = d end
return public
end
addbenchmark("Object using closures (noself)", "ob.test()", makeob4())

addbenchmark("Direct Access", "ob.data = ob.data + 1", makeob1())

addbenchmark("Local Variable", "ob = ob + 1", 0)

local loop_count = select(1,...) or 100000000
runbenchmarks(loop_count)
function make_ob_table()
    local arr = {
        a1=1,
        a2=2,
        a3=3,
        a4=4,
    }
    return {
        init = function ()
            arr = {
                a1=1,
                a2=2,
                a3=3,
                a4=4,
            }
        end,
        forin_and_set = function ()
            for k,v in pairs(arr) do
                arr[k] = v+1
            end
        end,
    }
end

clearbenchmark()

addbenchmark("table init", "ob.init()", make_ob_table())
addbenchmark("table forin_and_set", "ob.forin_and_set()", make_ob_table())

runbenchmarks(loop_count/4)

function make_ob_arr(size)
    size = size or 4
    local arr = {}
    for i = 1, size do
        arr[i] = i
    end
    return {
        init = function ()
            arr = {}
            for i = 1, size do
                arr[i] = i
            end
        end,
        reset = function ()
            for i = 1, size do
                arr[i] = i
            end
        end,
        forin = function ()
            for k,v in pairs(arr) do
                -- do nothing
            end
        end
    }
end

function test_array_size(arr_size)
    local loop_count = loop_count//arr_size
    if loop_count <= 0 then
        return
    end
    clearbenchmark()
    addbenchmark("array["..arr_size.."] init", "ob.init()", make_ob_arr(arr_size))
    addbenchmark("array["..arr_size.."] reset", "ob.reset()", make_ob_arr(arr_size))
    addbenchmark("array["..arr_size.."] forin", "ob.forin()", make_ob_arr(arr_size))
    runbenchmarks(loop_count)
end

test_array_size(2)
test_array_size(4)
test_array_size(20)
test_array_size(200)
test_array_size(2000)