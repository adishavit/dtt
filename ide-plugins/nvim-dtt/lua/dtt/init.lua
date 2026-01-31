-- DTT: Dangling Pointer and Type Safety Checker for Neovim
-- Provides real-time diagnostics for C/C++ files

local M = {}

local config = {
  clang_path = "clang++",
  plugin_path = nil,  -- Auto-detected or user-specified
  standard = "c++20",
  mode = "inform",
  debounce_ms = 500,
  analyze_on_save = true,
  analyze_on_change = true,
}

local ns = vim.api.nvim_create_namespace("dtt")
local timers = {}
local jobs = {}

-- Diagnostic parser (matches clang output format)
local function parse_diagnostics(output, filter_file)
  local diagnostics = {}
  local pattern = "^(.+):(%d+):(%d+):%s*(error|warning|note):%s*(.+)$"

  for line in output:gmatch("[^\r\n]+") do
    local file, lnum, col, severity, message = line:match(pattern)
    if file and message:find("DTT:") then
      -- Filter to current file if specified
      if not filter_file or file:match(filter_file .. "$") or file == filter_file then
        local sev = severity == "error" and vim.diagnostic.severity.ERROR
          or severity == "warning" and vim.diagnostic.severity.WARN
          or vim.diagnostic.severity.INFO

        table.insert(diagnostics, {
          lnum = tonumber(lnum) - 1,
          col = tonumber(col) - 1,
          message = message,
          severity = sev,
          source = "dtt",
        })
      end
    end
  end

  return diagnostics
end

-- Detect if running on Windows
local function is_windows()
  return vim.fn.has("win32") == 1 or vim.fn.has("win64") == 1
end

-- Find DTT plugin in common locations
local function find_plugin()
  if config.plugin_path and vim.fn.filereadable(config.plugin_path) == 1 then
    return config.plugin_path
  end

  local locations
  if is_windows() then
    local localappdata = vim.fn.getenv("LOCALAPPDATA") or ""
    local programfiles = vim.fn.getenv("ProgramFiles") or "C:/Program Files"
    locations = {
      vim.fn.expand("~/.dtt/bin/DTTPlugin.dll"),
      localappdata .. "/DTT/bin/DTTPlugin.dll",
      programfiles .. "/DTT/bin/DTTPlugin.dll",
      programfiles .. "/LLVM/lib/DTTPlugin.dll",
    }
  else
    locations = {
      -- User's home
      vim.fn.expand("~/.dtt/bin/libDTTPlugin.so"),
      vim.fn.expand("~/.dtt/bin/libDTTPlugin.dylib"),
      -- System locations
      "/usr/local/lib/libDTTPlugin.so",
      "/usr/lib/libDTTPlugin.so",
      -- Homebrew
      "/opt/homebrew/lib/libDTTPlugin.dylib",
      "/usr/local/lib/libDTTPlugin.dylib",
    }
  end

  for _, loc in ipairs(locations) do
    if vim.fn.filereadable(loc) == 1 then
      return loc
    end
  end

  return nil
end

-- Run DTT analysis
function M.analyze(bufnr)
  bufnr = bufnr or vim.api.nvim_get_current_buf()
  local filepath = vim.api.nvim_buf_get_name(bufnr)

  if filepath == "" then return end

  -- Cancel existing job for this buffer
  if jobs[bufnr] then
    vim.fn.jobstop(jobs[bufnr])
    jobs[bufnr] = nil
  end

  local plugin = find_plugin()
  if not plugin then
    vim.notify("DTT: Plugin not found. Set via require('dtt').setup({plugin_path = '...'})", vim.log.levels.WARN)
    return
  end

  local cmd
  if is_windows() then
    -- Windows requires -Xclang syntax for plugins
    cmd = {
      config.clang_path,
      "-Xclang", "-load", "-Xclang", plugin,
      "-Xclang", "-add-plugin", "-Xclang", "dtt",
      "-Xclang", "-plugin-arg-dtt", "-Xclang", "--dtt-mode=" .. config.mode,
      "-std=" .. config.standard,
      "-fsyntax-only",
      filepath,
    }
  else
    cmd = {
      config.clang_path,
      "-fplugin=" .. plugin,
      "-fplugin-arg-dtt---dtt-mode=" .. config.mode,
      "-std=" .. config.standard,
      "-fsyntax-only",
      filepath,
    }
  end

  local output = ""
  jobs[bufnr] = vim.fn.jobstart(cmd, {
    stdout_buffered = true,
    stderr_buffered = true,
    on_stderr = function(_, data)
      if data then
        output = output .. table.concat(data, "\n")
      end
    end,
    on_exit = function(_, exit_code)
      jobs[bufnr] = nil
      local diagnostics = parse_diagnostics(output, vim.fn.fnamemodify(filepath, ":t"))
      vim.diagnostic.set(ns, bufnr, diagnostics)
    end,
  })
end

-- Clear diagnostics
function M.clear(bufnr)
  bufnr = bufnr or vim.api.nvim_get_current_buf()
  vim.diagnostic.reset(ns, bufnr)
end

-- Debounced analysis
local function analyze_debounced(bufnr)
  if timers[bufnr] then
    vim.fn.timer_stop(timers[bufnr])
  end

  timers[bufnr] = vim.fn.timer_start(config.debounce_ms, function()
    timers[bufnr] = nil
    vim.schedule(function()
      M.analyze(bufnr)
    end)
  end)
end

-- Setup autocommands
local function setup_autocmds()
  local group = vim.api.nvim_create_augroup("DTT", { clear = true })

  if config.analyze_on_save then
    vim.api.nvim_create_autocmd("BufWritePost", {
      group = group,
      pattern = { "*.cpp", "*.cxx", "*.cc", "*.c", "*.hpp", "*.hxx", "*.h" },
      callback = function(ev)
        M.analyze(ev.buf)
      end,
    })
  end

  if config.analyze_on_change then
    vim.api.nvim_create_autocmd("TextChanged", {
      group = group,
      pattern = { "*.cpp", "*.cxx", "*.cc", "*.c", "*.hpp", "*.hxx", "*.h" },
      callback = function(ev)
        analyze_debounced(ev.buf)
      end,
    })

    vim.api.nvim_create_autocmd("TextChangedI", {
      group = group,
      pattern = { "*.cpp", "*.cxx", "*.cc", "*.c", "*.hpp", "*.hxx", "*.h" },
      callback = function(ev)
        analyze_debounced(ev.buf)
      end,
    })
  end

  vim.api.nvim_create_autocmd("BufDelete", {
    group = group,
    callback = function(ev)
      if jobs[ev.buf] then
        vim.fn.jobstop(jobs[ev.buf])
        jobs[ev.buf] = nil
      end
      if timers[ev.buf] then
        vim.fn.timer_stop(timers[ev.buf])
        timers[ev.buf] = nil
      end
    end,
  })
end

-- User commands
local function setup_commands()
  vim.api.nvim_create_user_command("DTTAnalyze", function()
    M.analyze()
  end, { desc = "Run DTT analysis on current buffer" })

  vim.api.nvim_create_user_command("DTTClear", function()
    M.clear()
  end, { desc = "Clear DTT diagnostics" })
end

-- Setup function
function M.setup(opts)
  config = vim.tbl_deep_extend("force", config, opts or {})
  setup_autocmds()
  setup_commands()
end

return M
