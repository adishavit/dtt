" DTT.vim - Dangling Pointer and Type Safety Checker
" Zero-config: auto-downloads clang and plugin if needed
" Integrates with ALE (Asynchronous Lint Engine)

if exists('g:loaded_dtt_ale')
  finish
endif
let g:loaded_dtt_ale = 1

" Configuration
let g:dtt_clang_path = get(g:, 'dtt_clang_path', '')
let g:dtt_plugin_path = get(g:, 'dtt_plugin_path', '')
let g:dtt_standard = get(g:, 'dtt_standard', 'c++20')
let g:dtt_mode = get(g:, 'dtt_mode', 'inform')
let g:dtt_auto_download = get(g:, 'dtt_auto_download', 1)

" Constants
let s:llvm_version = '18.1.8'
let s:dtt_version = 'latest'
let s:setup_done = 0
let s:setup_in_progress = 0

" Platform detection
function! s:IsWindows() abort
  return has('win32') || has('win64')
endfunction

function! s:IsMacOS() abort
  return has('mac') || has('macunix')
endfunction

function! s:GetArch() abort
  if s:IsMacOS()
    let l:arch = system('uname -m')
    if l:arch =~# 'arm64\|aarch64'
      return 'arm64'
    endif
  endif
  return 'x86_64'
endfunction

function! s:GetStorageDir() abort
  return expand('~/.dtt/bin')
endfunction

" Find clang in PATH and common locations
function! s:FindClang() abort
  if !empty(g:dtt_clang_path) && executable(g:dtt_clang_path)
    return g:dtt_clang_path
  endif

  " Check PATH
  if executable('clang++')
    return 'clang++'
  endif
  if executable('clang++-18')
    return 'clang++-18'
  endif

  " Check common locations
  if s:IsWindows()
    let l:locations = [
      \ 'C:/Program Files/LLVM/bin/clang++.exe',
      \ 'C:/LLVM/bin/clang++.exe',
      \ expand('~/.dtt/bin/llvm/bin/clang++.exe'),
      \ ]
  elseif s:IsMacOS()
    let l:locations = [
      \ '/opt/homebrew/opt/llvm/bin/clang++',
      \ '/usr/local/opt/llvm/bin/clang++',
      \ '/opt/homebrew/bin/clang++',
      \ expand('~/.dtt/bin/llvm/bin/clang++'),
      \ ]
  else
    let l:locations = [
      \ '/usr/bin/clang++-18',
      \ '/usr/bin/clang++',
      \ '/usr/local/bin/clang++',
      \ expand('~/.dtt/bin/llvm/bin/clang++'),
      \ ]
  endif

  for l:loc in l:locations
    if executable(l:loc)
      return l:loc
    endif
  endfor

  return ''
endfunction

" Find DTT plugin
function! s:FindPlugin() abort
  if !empty(g:dtt_plugin_path) && filereadable(g:dtt_plugin_path)
    return g:dtt_plugin_path
  endif

  let l:ext = s:IsWindows() ? '.dll' : (s:IsMacOS() ? '.dylib' : '.so')
  let l:name = s:IsWindows() ? 'DTTPlugin' : 'libDTTPlugin'

  if s:IsWindows()
    let l:localappdata = $LOCALAPPDATA
    let l:programfiles = empty($ProgramFiles) ? 'C:/Program Files' : $ProgramFiles
    let l:locations = [
      \ expand('~/.dtt/bin/' . l:name . l:ext),
      \ l:localappdata . '/DTT/bin/' . l:name . l:ext,
      \ l:programfiles . '/DTT/bin/' . l:name . l:ext,
      \ ]
  else
    let l:locations = [
      \ expand('~/.dtt/bin/' . l:name . l:ext),
      \ '/usr/local/lib/' . l:name . l:ext,
      \ '/usr/lib/' . l:name . l:ext,
      \ '/opt/homebrew/lib/' . l:name . l:ext,
      \ ]
  endif

  for l:loc in l:locations
    if filereadable(l:loc)
      return l:loc
    endif
  endfor

  return ''
endfunction

" Download file using curl or wget
function! s:DownloadFile(url, dest) abort
  call mkdir(fnamemodify(a:dest, ':h'), 'p')
  echom 'DTT: Downloading ' . fnamemodify(a:dest, ':t') . '...'

  if executable('curl')
    let l:cmd = 'curl -fSL --progress-bar -o ' . shellescape(a:dest) . ' ' . shellescape(a:url)
  elseif executable('wget')
    let l:cmd = 'wget -q --show-progress -O ' . shellescape(a:dest) . ' ' . shellescape(a:url)
  else
    echoerr 'DTT: Neither curl nor wget found'
    return 0
  endif

  let l:result = system(l:cmd)
  return v:shell_error == 0
endfunction

" Extract archive
function! s:ExtractArchive(archive, dest) abort
  call mkdir(a:dest, 'p')
  echom 'DTT: Extracting...'

  if a:archive =~# '\.tar\.xz$'
    let l:cmd = 'tar -xf ' . shellescape(a:archive) . ' -C ' . shellescape(a:dest) . ' --strip-components=1'
  elseif a:archive =~# '\.zip$'
    if s:IsWindows()
      let l:cmd = 'powershell -Command "Expand-Archive -Path ''' . a:archive . ''' -DestinationPath ''' . a:dest . ''' -Force"'
    else
      let l:cmd = 'unzip -q -o ' . shellescape(a:archive) . ' -d ' . shellescape(a:dest)
    endif
  else
    echoerr 'DTT: Unknown archive format'
    return 0
  endif

  call system(l:cmd)
  call delete(a:archive)
  return v:shell_error == 0
endfunction

" Download DTT plugin
function! s:DownloadPlugin() abort
  let l:storage = s:GetStorageDir()
  let l:ext = s:IsWindows() ? '.dll' : (s:IsMacOS() ? '.dylib' : '.so')
  let l:name = s:IsWindows() ? 'DTTPlugin' : 'libDTTPlugin'

  if s:IsWindows()
    let l:asset = 'DTTPlugin-win64.dll'
  elseif s:IsMacOS()
    let l:asset = 'libDTTPlugin-darwin.dylib'
  else
    let l:asset = 'libDTTPlugin-linux.so'
  endif

  let l:url = 'https://github.com/adishavit/dtt/releases/' . s:dtt_version . '/download/' . l:asset
  let l:dest = l:storage . '/' . l:name . l:ext

  if s:DownloadFile(l:url, l:dest)
    if !s:IsWindows()
      call system('chmod +x ' . shellescape(l:dest))
    endif
    echom 'DTT: Plugin installed!'
    return l:dest
  endif

  " macOS binaries not available - show build instructions
  if s:IsMacOS()
    echom 'DTT: macOS binary not available. Build from source:'
    echom '  brew install llvm@18 z3'
    echom '  git clone https://github.com/adishavit/dtt && cd dtt'
    echom '  cmake -B build && cmake --build build'
    echom '  cp build/libDTTPlugin.dylib ~/.dtt/bin/'
  endif

  return ''
endfunction

" Download LLVM
function! s:DownloadLLVM() abort
  let l:storage = s:GetStorageDir()
  let l:llvm_dir = l:storage . '/llvm'
  let l:arch = s:GetArch()

  if s:IsWindows()
    let l:filename = 'LLVM-' . s:llvm_version . '-win64'
    let l:ext = '.zip'
  elseif s:IsMacOS()
    let l:filename = 'clang+llvm-' . s:llvm_version . '-' . l:arch . '-apple-darwin'
    let l:ext = '.tar.xz'
  else
    let l:filename = 'clang+llvm-' . s:llvm_version . '-x86_64-linux-gnu-ubuntu-22.04'
    let l:ext = '.tar.xz'
  endif

  let l:url = 'https://github.com/llvm/llvm-project/releases/download/llvmorg-' . s:llvm_version . '/' . l:filename . l:ext
  let l:archive = l:storage . '/llvm' . l:ext

  echom 'DTT: Downloading LLVM ' . s:llvm_version . ' (this may take a few minutes)...'

  if s:DownloadFile(l:url, l:archive)
    if s:ExtractArchive(l:archive, l:llvm_dir)
      " Windows ZIP extracts with folder name intact, need to check both paths
      let l:clang_path = l:llvm_dir . '/bin/clang++' . (s:IsWindows() ? '.exe' : '')
      if executable(l:clang_path)
        echom 'DTT: LLVM installed!'
        return l:clang_path
      endif
      " Try nested path for Windows ZIP extraction
      if s:IsWindows()
        let l:nested_path = l:llvm_dir . '/' . l:filename . '/bin/clang++.exe'
        if executable(l:nested_path)
          echom 'DTT: LLVM installed!'
          return l:nested_path
        endif
      endif
    endif
  endif

  return ''
endfunction

" Ensure binaries are available
function! s:EnsureBinaries() abort
  if s:setup_done || s:setup_in_progress
    return s:setup_done
  endif
  let s:setup_in_progress = 1

  let l:clang = s:FindClang()
  let l:plugin = s:FindPlugin()

  " Both found
  if !empty(l:clang) && !empty(l:plugin)
    let g:dtt_clang_path = l:clang
    let g:dtt_plugin_path = l:plugin
    let s:setup_done = 1
    let s:setup_in_progress = 0
    return 1
  endif

  " Auto-download disabled
  if !g:dtt_auto_download
    let s:setup_in_progress = 0
    if empty(l:clang)
      echom 'DTT: Clang not found. Install LLVM 18+ or set g:dtt_clang_path'
    endif
    if empty(l:plugin)
      echom 'DTT: Plugin not found. Set g:dtt_plugin_path'
    endif
    return 0
  endif

  " Download what's missing
  if empty(l:plugin)
    let l:plugin = s:DownloadPlugin()
    if !empty(l:plugin)
      let g:dtt_plugin_path = l:plugin
    endif
  endif

  if empty(l:clang)
    let l:clang = s:DownloadLLVM()
    if !empty(l:clang)
      let g:dtt_clang_path = l:clang
    else
      echom 'DTT: Failed to download LLVM. Please install LLVM 18+ manually.'
    endif
  endif

  let g:dtt_clang_path = empty(g:dtt_clang_path) ? s:FindClang() : g:dtt_clang_path
  let g:dtt_plugin_path = empty(g:dtt_plugin_path) ? s:FindPlugin() : g:dtt_plugin_path

  if !empty(g:dtt_clang_path) && !empty(g:dtt_plugin_path)
    let s:setup_done = 1
    echom 'DTT: Ready!'
  endif

  let s:setup_in_progress = 0
  return s:setup_done
endfunction

" Get command for ALE
function! s:GetCommand(buffer) abort
  if !s:EnsureBinaries()
    return ''
  endif

  if s:IsWindows()
    return g:dtt_clang_path
      \ . ' -Xclang -load -Xclang ' . g:dtt_plugin_path
      \ . ' -Xclang -add-plugin -Xclang dtt'
      \ . ' -Xclang -plugin-arg-dtt -Xclang --dtt-mode=' . g:dtt_mode
      \ . ' -std=' . g:dtt_standard
      \ . ' -fsyntax-only %t'
  else
    return g:dtt_clang_path
      \ . ' -fplugin=' . g:dtt_plugin_path
      \ . ' -fplugin-arg-dtt---dtt-mode=' . g:dtt_mode
      \ . ' -std=' . g:dtt_standard
      \ . ' -fsyntax-only %t'
  endif
endfunction

" Register with ALE
function! s:RegisterALE() abort
  if !exists('g:ale_linters')
    let g:ale_linters = {}
  endif

  call ale#linter#Define('cpp', {
    \ 'name': 'dtt',
    \ 'executable': function('s:GetExecutable'),
    \ 'command': function('s:GetCommand'),
    \ 'callback': 'ale#handlers#gcc#HandleGCCFormat',
    \ 'output_stream': 'stderr',
    \ })

  call ale#linter#Define('c', {
    \ 'name': 'dtt',
    \ 'executable': function('s:GetExecutable'),
    \ 'command': function('s:GetCommand'),
    \ 'callback': 'ale#handlers#gcc#HandleGCCFormat',
    \ 'output_stream': 'stderr',
    \ })
endfunction

function! s:GetExecutable(buffer) abort
  call s:EnsureBinaries()
  return g:dtt_clang_path
endfunction

" Manual setup command
command! DTTSetup call s:EnsureBinaries()

" Auto-register if ALE is available
if exists('g:loaded_ale')
  call s:RegisterALE()
else
  " Defer registration until ALE loads via VimEnter
  augroup dtt_ale_init
    autocmd!
    autocmd VimEnter * if exists('g:loaded_ale') | call s:RegisterALE() | endif
    autocmd User AleFixPre if !exists('s:ale_registered') | call s:RegisterALE() | let s:ale_registered = 1 | endif
  augroup END
endif

" Auto-setup on first C/C++ file
augroup dtt_auto_setup
  autocmd!
  autocmd FileType c,cpp if !s:setup_done | call s:EnsureBinaries() | endif
augroup END
