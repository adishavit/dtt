-- DTT: Dangling Pointer and Type Safety Checker for Neovim
-- Zero-config: auto-downloads clang and plugin if needed

local M = {}

local config = {
  clang_path = nil,     -- Auto-detected or user-specified
  plugin_path = nil,    -- Auto-detected or user-specified
  standard = "c++20",
  mode = "inform",
  debounce_ms = 500,
  analyze_on_save = true,
  analyze_on_change = true,
  auto_download = true, -- Automatically download missing binaries
}

local ns = vim.api.nvim_create_namespace("dtt")
local timers = {}
local jobs = {}
local is_ready = false
local setup_in_progress = false

-- Constants
local DTT_VERSION = "latest"
local LLVM_VERSION = "18.1.8"

-- Detect platform
local function is_windows()
  return vim.fn.has("win32") == 1 or vim.fn.has("win64") == 1
end

local function is_macos()
  return vim.fn.has("mac") == 1 or vim.fn.has("macunix") == 1
end

local function get_arch()
  local arch = vim.fn.system("uname -m"):gsub("%s+", "")
  if arch == "arm64" or arch == "aarch64" then
    return "arm64"
  end
  return "x86_64"
end

-- Get storage directory
local function get_storage_dir()
  return vim.fn.expand("~/.dtt/bin")
end

-- Diagnostic parser
local function parse_diagnostics(output, filter_file)
  local diagnostics = {}
  local pattern = "^(.+):(%d+):(%d+):%s*(error|warning|note):%s*(.+)$"

  for line in output:gmatch("[^\r\n]+") do
    local file, lnum, col, severity, message = line:match(pattern)
    if file and message:find("DTT:") then
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

-- Find clang in PATH and common locations
local function find_clang()
  if config.clang_path and vim.fn.executable(config.clang_path) == 1 then
    return config.clang_path
  end

  -- Check PATH
  if vim.fn.executable("clang++") == 1 then
    return "clang++"
  end
  if vim.fn.executable("clang++-18") == 1 then
    return "clang++-18"
  end

  -- Check common locations
  local locations
  if is_windows() then
    locations = {
      "C:/Program Files/LLVM/bin/clang++.exe",
      "C:/LLVM/bin/clang++.exe",
      vim.fn.expand("~/.dtt/bin/llvm/bin/clang++.exe"),
    }
  elseif is_macos() then
    locations = {
      "/opt/homebrew/opt/llvm/bin/clang++",
      "/usr/local/opt/llvm/bin/clang++",
      "/opt/homebrew/bin/clang++",
      vim.fn.expand("~/.dtt/bin/llvm/bin/clang++"),
    }
  else
    locations = {
      "/usr/bin/clang++-18",
      "/usr/bin/clang++",
      "/usr/local/bin/clang++",
      vim.fn.expand("~/.dtt/bin/llvm/bin/clang++"),
    }
  end

  for _, loc in ipairs(locations) do
    if vim.fn.executable(loc) == 1 then
      return loc
    end
  end

  return nil
end

-- Find DTT plugin
local function find_plugin()
  if config.plugin_path and vim.fn.filereadable(config.plugin_path) == 1 then
    return config.plugin_path
  end

  local ext = is_windows() and ".dll" or (is_macos() and ".dylib" or ".so")
  local name = is_windows() and "DTTPlugin" or "libDTTPlugin"

  local locations
  if is_windows() then
    local localappdata = vim.fn.getenv("LOCALAPPDATA") or ""
    local programfiles = vim.fn.getenv("ProgramFiles") or "C:/Program Files"
    locations = {
      vim.fn.expand("~/.dtt/bin/" .. name .. ext),
      localappdata .. "/DTT/bin/" .. name .. ext,
      programfiles .. "/DTT/bin/" .. name .. ext,
    }
  else
    locations = {
      vim.fn.expand("~/.dtt/bin/" .. name .. ext),
      "/usr/local/lib/" .. name .. ext,
      "/usr/lib/" .. name .. ext,
      "/opt/homebrew/lib/" .. name .. ext,
    }
  end

  for _, loc in ipairs(locations) do
    if vim.fn.filereadable(loc) == 1 then
      return loc
    end
  end

  return nil
end

-- Download file using curl or wget
local function download_file(url, dest, callback)
  vim.fn.mkdir(vim.fn.fnamemodify(dest, ":h"), "p")

  local cmd
  if vim.fn.executable("curl") == 1 then
    cmd = { "curl", "-fSL", "--progress-bar", "-o", dest, url }
  elseif vim.fn.executable("wget") == 1 then
    cmd = { "wget", "-q", "--show-progress", "-O", dest, url }
  else
    callback(false, "Neither curl nor wget found")
    return
  end

  vim.notify("DTT: Downloading " .. vim.fn.fnamemodify(dest, ":t") .. "...", vim.log.levels.INFO)

  vim.fn.jobstart(cmd, {
    on_exit = function(_, code)
      if code == 0 then
        callback(true)
      else
        callback(false, "Download failed with code " .. code)
      end
    end,
  })
end

-- Extract archive
local function extract_archive(archive, dest, callback)
  vim.fn.mkdir(dest, "p")

  local cmd
  if archive:match("%.tar%.xz$") then
    cmd = { "tar", "-xf", archive, "-C", dest, "--strip-components=1" }
  elseif archive:match("%.zip$") then
    if is_windows() then
      cmd = { "powershell", "-Command", string.format("Expand-Archive -Path '%s' -DestinationPath '%s' -Force", archive, dest) }
    else
      cmd = { "unzip", "-q", "-o", archive, "-d", dest }
    end
  else
    callback(false, "Unknown archive format")
    return
  end

  vim.notify("DTT: Extracting...", vim.log.levels.INFO)

  vim.fn.jobstart(cmd, {
    on_exit = function(_, code)
      vim.fn.delete(archive)
      if code == 0 then
        callback(true)
      else
        callback(false, "Extraction failed")
      end
    end,
  })
end

-- Download LLVM/Clang
local function download_llvm(callback)
  local storage = get_storage_dir()
  local llvm_dir = storage .. "/llvm"

  local filename, ext
  if is_windows() then
    filename = string.format("LLVM-%s-win64", LLVM_VERSION)
    ext = ".zip"
  elseif is_macos() then
    local arch = get_arch()
    filename = string.format("clang+llvm-%s-%s-apple-darwin", LLVM_VERSION, arch)
    ext = ".tar.xz"
  else
    filename = string.format("clang+llvm-%s-x86_64-linux-gnu-ubuntu-22.04", LLVM_VERSION)
    ext = ".tar.xz"
  end

  local url = string.format("https://github.com/llvm/llvm-project/releases/download/llvmorg-%s/%s%s", LLVM_VERSION, filename, ext)
  local archive_path = storage .. "/llvm" .. ext

  vim.notify("DTT: Downloading LLVM " .. LLVM_VERSION .. " (this may take a few minutes)...", vim.log.levels.INFO)

  download_file(url, archive_path, function(ok, err)
    if not ok then
      callback(nil, err)
      return
    end

    extract_archive(archive_path, llvm_dir, function(extract_ok, extract_err)
      if not extract_ok then
        callback(nil, extract_err)
        return
      end

      local clang_path = llvm_dir .. "/bin/clang++" .. (is_windows() and ".exe" or "")
      if vim.fn.executable(clang_path) == 1 then
        vim.notify("DTT: LLVM installed successfully!", vim.log.levels.INFO)
        callback(clang_path)
      else
        callback(nil, "Clang not found after extraction")
      end
    end)
  end)
end

-- Download DTT plugin
local function download_plugin(callback)
  local storage = get_storage_dir()
  vim.fn.mkdir(storage, "p")

  local ext = is_windows() and ".dll" or (is_macos() and ".dylib" or ".so")
  local name = is_windows() and "DTTPlugin" or "libDTTPlugin"
  local asset = is_windows() and "DTTPlugin-win64.dll"
    or is_macos() and "libDTTPlugin-darwin.dylib"
    or "libDTTPlugin-linux.so"

  local url = string.format("https://github.com/adishavit/dtt/releases/%s/download/%s", DTT_VERSION, asset)
  local dest = storage .. "/" .. name .. ext

  download_file(url, dest, function(ok, err)
    if not ok then
      -- macOS binaries not available in releases - guide user to build from source
      if is_macos() then
        vim.notify(
          "DTT: macOS binary not available. Build from source:\n" ..
          "  brew install llvm@18 z3\n" ..
          "  git clone https://github.com/adishavit/dtt && cd dtt\n" ..
          "  cmake -B build && cmake --build build\n" ..
          "  cp build/libDTTPlugin.dylib ~/.dtt/bin/",
          vim.log.levels.WARN
        )
      end
      callback(nil, err)
      return
    end

    -- Make executable on Unix
    if not is_windows() then
      vim.fn.system({ "chmod", "+x", dest })
    end

    vim.notify("DTT: Plugin installed successfully!", vim.log.levels.INFO)
    callback(dest)
  end)
end

-- Ensure all binaries are available
local function ensure_binaries(callback)
  if setup_in_progress then
    return
  end
  setup_in_progress = true

  local clang = find_clang()
  local plugin = find_plugin()

  -- Both found - ready to go
  if clang and plugin then
    config.clang_path = clang
    config.plugin_path = plugin
    is_ready = true
    setup_in_progress = false
    if callback then callback(true) end
    return
  end

  -- Auto-download disabled
  if not config.auto_download then
    setup_in_progress = false
    if not clang then
      vim.notify("DTT: Clang not found. Install LLVM 18+ or set clang_path in setup()", vim.log.levels.WARN)
    end
    if not plugin then
      vim.notify("DTT: Plugin not found. Download from GitHub or set plugin_path in setup()", vim.log.levels.WARN)
    end
    if callback then callback(false) end
    return
  end

  -- Need to download something
  local function finish_setup()
    config.clang_path = config.clang_path or find_clang()
    config.plugin_path = config.plugin_path or find_plugin()

    if config.clang_path and config.plugin_path then
      is_ready = true
      vim.notify("DTT: Ready! Open a C/C++ file to start.", vim.log.levels.INFO)
      if callback then callback(true) end
    else
      if callback then callback(false) end
    end
    setup_in_progress = false
  end

  -- Download plugin first (smaller)
  if not plugin then
    download_plugin(function(plugin_path, err)
      if plugin_path then
        config.plugin_path = plugin_path
      else
        vim.notify("DTT: Failed to download plugin: " .. (err or "unknown error"), vim.log.levels.ERROR)
      end

      -- Then download clang if needed
      if not clang then
        download_llvm(function(clang_path, clang_err)
          if clang_path then
            config.clang_path = clang_path
          else
            vim.notify("DTT: Failed to download LLVM: " .. (clang_err or "unknown error"), vim.log.levels.ERROR)
            vim.notify("DTT: Please install LLVM 18+ manually", vim.log.levels.WARN)
          end
          finish_setup()
        end)
      else
        config.clang_path = clang
        finish_setup()
      end
    end)
  elseif not clang then
    config.plugin_path = plugin
    download_llvm(function(clang_path, err)
      if clang_path then
        config.clang_path = clang_path
      else
        vim.notify("DTT: Failed to download LLVM: " .. (err or "unknown error"), vim.log.levels.ERROR)
        vim.notify("DTT: Please install LLVM 18+ manually", vim.log.levels.WARN)
      end
      finish_setup()
    end)
  end
end

-- Run DTT analysis
function M.analyze(bufnr)
  bufnr = bufnr or vim.api.nvim_get_current_buf()

  if not is_ready then
    ensure_binaries(function(ok)
      if ok then
        M.analyze(bufnr)
      end
    end)
    return
  end

  local filepath = vim.api.nvim_buf_get_name(bufnr)
  if filepath == "" then return end

  -- Cancel existing job
  if jobs[bufnr] then
    vim.fn.jobstop(jobs[bufnr])
    jobs[bufnr] = nil
  end

  local cmd
  if is_windows() then
    cmd = {
      config.clang_path,
      "-Xclang", "-load", "-Xclang", config.plugin_path,
      "-Xclang", "-add-plugin", "-Xclang", "dtt",
      "-Xclang", "-plugin-arg-dtt", "-Xclang", "--dtt-mode=" .. config.mode,
      "-std=" .. config.standard,
      "-fsyntax-only",
      filepath,
    }
  else
    cmd = {
      config.clang_path,
      "-fplugin=" .. config.plugin_path,
      "-fplugin-arg-dtt---dtt-mode=" .. config.mode,
      "-std=" .. config.standard,
      "-fsyntax-only",
      filepath,
    }
  end

  local output = ""
  local job_id = vim.fn.jobstart(cmd, {
    stdout_buffered = true,
    stderr_buffered = true,
    on_stderr = function(_, data)
      if data then
        output = output .. table.concat(data, "\n")
      end
    end,
    on_exit = function(_, _)
      jobs[bufnr] = nil
      if vim.api.nvim_buf_is_valid(bufnr) then
        local diagnostics = parse_diagnostics(output, vim.fn.fnamemodify(filepath, ":t"))
        vim.diagnostic.set(ns, bufnr, diagnostics)
      end
    end,
  })

  if job_id <= 0 then
    vim.notify("DTT: Failed to start analysis", vim.log.levels.ERROR)
    return
  end

  jobs[bufnr] = job_id
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
      if vim.api.nvim_buf_is_valid(bufnr) then
        M.analyze(bufnr)
      end
    end)
  end)
end

-- Setup autocommands
local function setup_autocmds()
  local group = vim.api.nvim_create_augroup("DTT", { clear = true })

  -- Auto-setup on first C++ file
  vim.api.nvim_create_autocmd("FileType", {
    group = group,
    pattern = { "cpp", "c" },
    callback = function()
      if not is_ready and not setup_in_progress then
        ensure_binaries()
      end
    end,
  })

  if config.analyze_on_save then
    vim.api.nvim_create_autocmd("BufWritePost", {
      group = group,
      pattern = { "*.cpp", "*.cxx", "*.cc", "*.c", "*.hpp", "*.hxx", "*.h" },
      callback = function(ev)
        if is_ready then
          M.analyze(ev.buf)
        end
      end,
    })
  end

  if config.analyze_on_change then
    vim.api.nvim_create_autocmd({ "TextChanged", "TextChangedI" }, {
      group = group,
      pattern = { "*.cpp", "*.cxx", "*.cc", "*.c", "*.hpp", "*.hxx", "*.h" },
      callback = function(ev)
        if is_ready then
          analyze_debounced(ev.buf)
        end
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

  vim.api.nvim_create_user_command("DTTSetup", function()
    ensure_binaries(function(ok)
      if ok then
        vim.notify("DTT: Setup complete!", vim.log.levels.INFO)
      end
    end)
  end, { desc = "Download and setup DTT binaries" })
end

-- Setup function
function M.setup(opts)
  config = vim.tbl_deep_extend("force", config, opts or {})
  setup_autocmds()
  setup_commands()

  -- Check if already configured
  if config.clang_path and config.plugin_path then
    if vim.fn.executable(config.clang_path) == 1 and vim.fn.filereadable(config.plugin_path) == 1 then
      is_ready = true
    end
  end
end

return M
