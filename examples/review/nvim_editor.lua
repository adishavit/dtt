local root = vim.fn.getcwd()
vim.opt.rtp:prepend(root .. '/ide-plugins/nvim-dtt')
local dtt = require('dtt')
local dir = vim.fn.tempname()
vim.fn.mkdir(dir, 'p')
local source = dir .. '/input with spaces.cpp'
vim.fn.writefile({'#include <vector>'}, dir .. '/local.h')
local function code(unsafe)
  return {'#include "local.h"', 'void f(){std::vector<int> v' .. (unsafe and '' or '{1}') .. '; v.front();}'}
end
local function run()
  dtt.setup({clang_path = '/usr/bin/clang++-18', plugin_path = root .. '/build/libDTTPlugin.so',
    auto_download = false, analyze_on_change = false, analyze_on_save = false})
  for _, unsafe in ipairs({true, false}) do
    vim.fn.writefile(code(not unsafe), source)
    vim.cmd('edit! ' .. vim.fn.fnameescape(source))
    local buf = vim.api.nvim_get_current_buf()
    vim.api.nvim_buf_set_lines(buf, 0, -1, false, code(unsafe))
    local done, result = false, nil
    local original = vim.diagnostic.set
    vim.diagnostic.set = function(ns, b, diagnostics, opts)
      if b == buf then done = true; result = diagnostics end
      return original(ns, b, diagnostics, opts)
    end
    dtt.analyze(buf)
    assert(vim.wait(10000, function() return done end), 'analysis did not finish')
    vim.diagnostic.set = original
    local errors = vim.tbl_filter(function(d) return d.severity == vim.diagnostic.severity.ERROR end, result)
    assert((#errors > 0) == unsafe, 'result reflects disk instead of current buffer')
    assert(vim.deep_equal(vim.fn.readfile(source), code(not unsafe)), 'analysis modified saved source')
  end
  -- Deliver an old process's completion after its replacement has started.
  local start, stop, set = vim.fn.jobstart, vim.fn.jobstop, vim.diagnostic.set
  local runs, writes = {}, 0
  vim.fn.jobstart = function(cmd, opts) table.insert(runs, {cmd = cmd, opts = opts}); return 9000 + #runs end
  vim.fn.jobstop = function() return 1 end
  vim.diagnostic.set = function() writes = writes + 1 end
  dtt.analyze(); dtt.analyze()
  runs[1].opts.on_exit(9001, 0)
  assert(writes == 0, 'superseded result published')
  runs[2].opts.on_exit(9002, 0)
  assert(writes == 1, 'replacement was untracked by old completion')
  dtt.analyze()
  vim.api.nvim_buf_set_lines(0, 0, -1, false, {'// changed while analysis runs'})
  runs[3].opts.on_exit(9003, 0)
  assert(writes == 1, 'outdated document version published')
  for _, r in ipairs(runs) do
    local overlay = r.cmd[#r.cmd - 1]
    assert(vim.fn.filereadable(overlay) == 0, 'snapshot leaked after completion')
  end
  vim.fn.jobstart, vim.fn.jobstop, vim.diagnostic.set = start, stop, set
  local wrong = dir .. '/wrong-clang'
  vim.fn.writefile({'#!/bin/sh', "echo 'clang version 19.0.0'"}, wrong)
  vim.fn.setfperm(wrong, 'rwx------')
  dtt.setup({clang_path = wrong, plugin_path = root .. '/build/libDTTPlugin.so', auto_download = false})
  for i = 1, 100 do
    local name, value = debug.getupvalue(dtt.analyze, i)
    if name == 'is_ready' then assert(not value, 'incompatible compiler marked ready'); break end
  end
  local broken = dir .. '/broken.so' 
  vim.fn.writefile({'not a plugin'}, broken)
  dtt.setup({clang_path = '/usr/bin/clang++-18', plugin_path = broken, auto_download = false})
  for i = 1, 100 do
    local name, value = debug.getupvalue(dtt.analyze, i)
    if name == 'is_ready' then assert(not value, 'broken plugin marked ready'); break end
  end
end
local ok, err = pcall(run)
vim.fn.delete(dir, 'rf')
if not ok then print(err); vim.cmd('cquit 1') end
print('Neovim unsaved buffer and local include checks passed')
vim.cmd('qa!')
