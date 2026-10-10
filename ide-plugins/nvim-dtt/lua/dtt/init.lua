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

-- Checksum verification helpers
local function compute_file_hash(filepath)
  -- Use sha256sum or openssl for computing hash
  local cmd
  if vim.fn.executable("sha256sum") == 1 then
    cmd = { "sha256sum", filepath }
  elseif vim.fn.executable("shasum") == 1 then
    cmd = { "shasum", "-a", "256", filepath }
  elseif vim.fn.executable("openssl") == 1 then
    cmd = { "openssl", "dgst", "-sha256", filepath }
  else
    return nil
  end

  local result = vim.fn.system(cmd)
  if vim.v.shell_error ~= 0 then
    return nil
  end

  -- Extract hash from output (format varies by tool)
  local hash = result:match("([a-f0-9]+)")
  return hash and hash:lower() or nil
end

local function fetch_checksums(callback)
  local checksums = {}
  local url = string.format("https://github.com/adishavit/dtt/releases/%s/download/checksums.txt", DTT_VERSION)
  local dest = vim.fn.tempname()

  local cmd
  if vim.fn.executable("curl") == 1 then
    cmd = { "curl", "-fsSL", "-o", dest, url }
  elseif vim.fn.executable("wget") == 1 then
    cmd = { "wget", "-q", "-O", dest, url }
  else
    callback(checksums)
    return
  end

  vim.fn.jobstart(cmd, {
    on_exit = function(_, code)
      if code == 0 and vim.fn.filereadable(dest) == 1 then
        local content = vim.fn.readfile(dest)
        for _, line in ipairs(content) do
          -- Format: "hash  filename"
          local hash, filename = line:match("^([a-f0-9]+)%s+(.+)$")
          if hash and filename then
            checksums[filename:gsub("^%s*(.-)%s*$", "%1")] = hash:lower()
          end
        end
        vim.fn.delete(dest)
      end
      callback(checksums)
    end,
  })
end

local function verify_checksum(filepath, expected_hash, filename, callback)
  if not expected_hash or expected_hash == "" then
    vim.notify("DTT: Warning - No checksum available for " .. filename .. ". Skipping verification.", vim.log.levels.WARN)
    callback(true)
    return
  end

  local actual_hash = compute_file_hash(filepath)
  if not actual_hash then
    vim.notify("DTT: Unable to compute checksum (no sha256sum/shasum/openssl). Skipping verification.", vim.log.levels.WARN)
    callback(true)
    return
  end

  if actual_hash == expected_hash:lower() then
    callback(true)
  else
    vim.notify(string.format(
      "DTT: Checksum verification failed for %s!\n  Expected: %s\n  Got: %s",
      filename, expected_hash, actual_hash
    ), vim.log.levels.ERROR)
    vim.fn.delete(filepath)
    callback(false)
  end
end

-- Detect platform
local function is_windows()
  return vim.fn.has("win32") == 1 or vim.fn.has("win64") == 1
end

local function is_macos()
  return vim.fn.has("mac") == 1 or vim.fn.has("macunix") == 1
end

local function get_arch()
  local arch = is_windows() and vim.fn.getenv("PROCESSOR_ARCHITECTURE") or vim.fn.system("uname -m")
  arch = arch:lower():gsub("%s+", "")
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
  local pattern = "^(.+):(%d+):(%d+):%s*(%a+):%s*(.+)$"

  for line in output:gmatch("[^\r\n]+") do
    local file, lnum, col, severity, message = line:match(pattern)
    if file and (severity == "error" or severity == "warning" or severity == "note")
      and message:find("DTT:", 1, true) then
      -- Literal comparison: file names may contain Lua pattern characters.
      local basename = file:match("[^/\\]+$")
      if not filter_file or file == filter_file or basename == filter_file then
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

-- Bounded startup checks catch wrong compiler versions and missing plugin runtimes.
local function probe_command(cmd)
  local output = ""
  local collect = function(_, data) if data then output = output .. table.concat(data, "\n") end end
  local job = vim.fn.jobstart(cmd, {stdout_buffered = true, stderr_buffered = true,
    on_stdout = collect, on_stderr = collect})
  if job <= 0 then return -1, "Could not start compiler" end
  local code = vim.fn.jobwait({job}, 5000)[1]
  if code < 0 then vim.fn.jobstop(job) end
  return code, output
end

local function compatible_clang(candidate)
  local code, output = probe_command({candidate, "--version"})
  return code == 0 and output:match("clang version 18[%.%s]") ~= nil and
    not output:find("Apple clang", 1, true)
end

local function validate_toolchain()
  if not config.clang_path or not config.plugin_path or not compatible_clang(config.clang_path) then
    return false
  end
  local source = vim.fn.tempname() .. ".cpp"
  vim.fn.writefile({"void check(){int *p=nullptr; *p=1;}"}, source)
  local cmd = {config.clang_path}
  local flags = is_windows() and {"-Xclang", "-load", "-Xclang", config.plugin_path,
    "-Xclang", "-add-plugin", "-Xclang", "dtt"} or {"-fplugin=" .. config.plugin_path}
  vim.list_extend(cmd, flags)
  vim.list_extend(cmd, {"-std=c++20", "-fsyntax-only", source})
  local code, output = probe_command(cmd)
  vim.fn.delete(source)
  local ok = code == 1 and output:match("error: DTT:.-null pointer") ~= nil
  if not ok then
    vim.notify("DTT: Compiler/plugin load check failed. Use the matching Clang 18 build and Z3 runtime.\n" .. output,
      vim.log.levels.ERROR)
  end
  return ok
end

-- Find clang in PATH and common locations
local function find_clang()
  if config.clang_path then
    return compatible_clang(config.clang_path) and config.clang_path or nil
  end

  -- Check PATH
  if vim.fn.executable("clang++-18") == 1 and compatible_clang("clang++-18") then
    return "clang++-18"
  end
  if vim.fn.executable("clang++") == 1 and compatible_clang("clang++") then
    return "clang++"
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
      "/opt/homebrew/opt/llvm@18/bin/clang++",
      "/usr/local/opt/llvm@18/bin/clang++",
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
    if vim.fn.executable(loc) == 1 and compatible_clang(loc) then
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

-- Extract archive - uses safe argument passing to prevent injection
local function extract_archive(archive, dest, callback)
  vim.fn.mkdir(dest, "p")

  local cmd
  if archive:match("%.tar%.xz$") then
    -- tar with safe argument passing
    cmd = { "tar", "-xf", archive, "-C", dest, "--strip-components=1", "--no-same-owner" }
  elseif archive:match("%.zip$") then
    if is_windows() then
      -- PowerShell: use -args to safely pass paths without injection risk
      cmd = {
        "powershell", "-NoProfile", "-NonInteractive",
        "-ExecutionPolicy", "Bypass",
        "-Command",
        "Expand-Archive -LiteralPath $args[0] -DestinationPath $args[1] -Force",
        "-args", archive, dest
      }
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
        callback(false, "Extraction failed with code " .. code)
      end
    end,
  })
end

-- Download LLVM/Clang
local function download_llvm(callback)
  local storage = get_storage_dir()
  local llvm_dir = storage .. "/llvm"

  local arch = get_arch()
  local filename, ext
  if is_windows() then
    if arch == "arm64" then
      callback(nil, "Install a matching Clang 18 toolchain manually on Windows ARM64")
      return
    end
    filename = string.format("clang+llvm-%s-x86_64-pc-windows-msvc", LLVM_VERSION)
    ext = ".tar.xz"
  elseif is_macos() then
    if arch ~= "arm64" then
      callback(nil, "Install Clang 18 with: brew install llvm@18")
      return
    end
    filename = string.format("clang+llvm-%s-arm64-apple-macos11", LLVM_VERSION)
    ext = ".tar.xz"
  else
    local suffix = arch == "arm64" and "aarch64-linux-gnu" or "x86_64-linux-gnu-ubuntu-18.04"
    filename = string.format("clang+llvm-%s-%s", LLVM_VERSION, suffix)
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

-- Download DTT plugin with checksum verification
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

  -- Fetch checksums first
  fetch_checksums(function(checksums)
    local expected_hash = checksums[asset]

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

      -- Verify checksum
      verify_checksum(dest, expected_hash, asset, function(verified)
        if not verified then
          callback(nil, "Checksum verification failed")
          return
        end

        -- Make executable on Unix
        if not is_windows() then
          vim.fn.system({ "chmod", "+x", dest })
        end

        vim.notify("DTT: Plugin installed successfully!", vim.log.levels.INFO)
        callback(dest)
      end)
    end)
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
    is_ready = validate_toolchain()
    setup_in_progress = false
    if callback then callback(is_ready) end
    return
  end

  -- Auto-download disabled
  if not config.auto_download then
    setup_in_progress = false
    if not clang then
      vim.notify("DTT: Clang not found. Install matching LLVM 18 or set clang_path in setup()", vim.log.levels.WARN)
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
      is_ready = validate_toolchain()
      if is_ready then vim.notify("DTT: Ready! Open a C/C++ file to start.", vim.log.levels.INFO) end
      if callback then callback(is_ready) end
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

  local tick = vim.api.nvim_buf_get_changedtick(bufnr)
  local snapshot = vim.fn.tempname()
  local ok, err = pcall(function()
    vim.fn.mkdir(snapshot, "p", 448)
    vim.fn.writefile(vim.api.nvim_buf_get_lines(bufnr, 0, -1, false), snapshot .. "/source.cpp")
    vim.fn.writefile({vim.fn.json_encode({version = 0, ["use-external-names"] = false,
      roots = {{type = "file", name = filepath, ["external-contents"] = snapshot .. "/source.cpp"}}})},
      snapshot .. "/overlay.json")
  end)
  if not ok then
    vim.fn.delete(snapshot, "rf")
    vim.notify("DTT: Cannot prepare document snapshot: " .. tostring(err), vim.log.levels.ERROR)
    return
  end
  table.insert(cmd, #cmd, "-ivfsoverlay")
  table.insert(cmd, #cmd, snapshot .. "/overlay.json")
  local output, timeout, timed_out = "", nil, false
  local job_id = vim.fn.jobstart(cmd, {
    stdout_buffered = true,
    stderr_buffered = true,
    on_stderr = function(_, data)
      if data then output = output .. table.concat(data, "\n") end
    end,
    on_exit = function(id, code)
      if timeout then vim.fn.timer_stop(timeout) end
      vim.fn.delete(snapshot, "rf")
      if jobs[bufnr] ~= id then return end
      jobs[bufnr] = nil
      if vim.api.nvim_buf_is_valid(bufnr) and vim.api.nvim_buf_get_changedtick(bufnr) == tick then
        local diagnostics = parse_diagnostics(output, vim.fn.fnamemodify(filepath, ":t"))
        local failed = timed_out or (code ~= 0 and code ~= 1)
        local dtt_error = false
        for _, d in ipairs(diagnostics) do
          if d.severity == vim.diagnostic.severity.ERROR then dtt_error = true end
        end
        for line in output:gmatch("[^\n]+") do
          if line:match("error:") and not line:find("DTT:", 1, true) then failed = true end
        end
        if failed or (code ~= 0 and not dtt_error) then
          diagnostics = {{lnum = 0, col = 0, severity = vim.diagnostic.severity.WARN,
            source = "DTT", message = "DTT analysis failed: " .. (timed_out and "timed out" or output)}}
        end
        vim.diagnostic.set(ns, bufnr, diagnostics)
      end
    end,
  })
  if job_id <= 0 then
    vim.fn.delete(snapshot, "rf")
    vim.notify("DTT: Failed to start analysis", vim.log.levels.ERROR)
    return
  end
  jobs[bufnr] = job_id
  timeout = vim.fn.timer_start(30000, function()
    if jobs[bufnr] == job_id then timed_out = true; vim.fn.jobstop(job_id) end
  end)
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
  is_ready = false
  config = vim.tbl_deep_extend("force", config, opts or {})
  setup_autocmds()
  setup_commands()

  -- Check if already configured
  if config.clang_path and config.plugin_path then
    if vim.fn.executable(config.clang_path) == 1 and vim.fn.filereadable(config.plugin_path) == 1 then
      is_ready = validate_toolchain()
    end
  end
end

return M
