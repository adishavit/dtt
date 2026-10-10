-- Exercise the real parser without requiring an installed Neovim UI.
vim = {
  api = { nvim_create_namespace = function() return 1 end },
  diagnostic = { severity = { ERROR = 1, WARN = 2, INFO = 3 } },
}
local plugin = dofile(arg[1])
local parser
for i = 1, 100 do
  local name, value = debug.getupvalue(plugin.analyze, i)
  if not name then break end
  if name == "parse_diagnostics" then parser = value; break end
end
assert(parser, "parser unavailable")
local file = "/project/a+[1].cpp"
local result = parser(table.concat({
  file .. ":3:7: error: DTT: unsafe access",
  file .. ":4:2: warning: DTT: cannot prove bounds",
  file .. ":5:1: note: DTT: property established here",
  file .. ":6:1: fatal: DTT: invalid severity",
  file .. ":7:1: error: ordinary compiler error",
  "/elsewhere/b.cpp:2:1: error: DTT: another file",
}, "\r\n"), file)
assert(#result == 3, "expected error, warning, note; got " .. #result)
for i = 1, 3 do assert(result[i].severity == i) end
assert(result[1].lnum == 2 and result[1].col == 6)
assert(#parser("C:/Program Files/a.cpp:12:4: error: DTT: bad", "C:/Program Files/a.cpp") == 1)
assert(#parser("/elsewhere/ab.cpp:1:1: error: DTT: bad", "a.cpp") == 0)
print("Neovim diagnostics: PASS")
