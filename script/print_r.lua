local type = type
local pairs = pairs
local tostring = tostring

printl = print
print = cgi.print
echo = cgi.print

function print_r(sth, how, exp)
	exp = exp or "\r\n";
	_how = type(how) == "function" and how or print
    if type(sth) ~= "table" then
        _how(sth) 
        return
    end

    local space, deep = string.rep(' ', 4), 0
    local function _dump(t)
        for k,v in pairs(t) do
            local key = tostring(k)
            
            if type(v) == "table" then 
                deep = deep + 2 
                _how(string.format("%s[%s] => Table\r\n%s(\r",
                                string.rep(space, deep - 1),
                                key,
                                string.rep(space, deep)
                        )
                    ) --print.
                _dump(v)
                
                _how(string.format("%s)\r",string.rep(space, deep)))
                deep = deep - 2 
            else
                _how(string.format("%s[%s] => %s\r",
                                string.rep(space, deep + 1),
                                key,
                                tostring(v)
                        )
                    ) --print.
            end
        end
    end

    _how(string.format("Table\r\n(\r"))
    _dump(sth)
    _how(string.format(")\r"))
end

printl_r = function(sth) print_r(sth, printl) end
