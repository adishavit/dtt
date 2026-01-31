" DTT.vim - Dangling Pointer and Type Safety Checker
" ALE (Asynchronous Lint Engine) linter for DTT

if exists('g:loaded_dtt_ale')
  finish
endif
let g:loaded_dtt_ale = 1

" Configuration
let g:dtt_clang_path = get(g:, 'dtt_clang_path', 'clang++')
let g:dtt_plugin_path = get(g:, 'dtt_plugin_path', '')
let g:dtt_standard = get(g:, 'dtt_standard', 'c++20')
let g:dtt_mode = get(g:, 'dtt_mode', 'inform')

" Detect if running on Windows
function! s:IsWindows() abort
  return has('win32') || has('win64')
endfunction

" Find DTT plugin in common locations
function! s:FindPlugin() abort
  if !empty(g:dtt_plugin_path) && filereadable(g:dtt_plugin_path)
    return g:dtt_plugin_path
  endif

  if s:IsWindows()
    let l:localappdata = $LOCALAPPDATA
    let l:programfiles = $ProgramFiles
    if empty(l:programfiles)
      let l:programfiles = 'C:/Program Files'
    endif
    let l:locations = [
      \ expand('~/.dtt/bin/DTTPlugin.dll'),
      \ l:localappdata . '/DTT/bin/DTTPlugin.dll',
      \ l:programfiles . '/DTT/bin/DTTPlugin.dll',
      \ l:programfiles . '/LLVM/lib/DTTPlugin.dll',
      \ ]
  else
    let l:locations = [
      \ expand('~/.dtt/bin/libDTTPlugin.so'),
      \ expand('~/.dtt/bin/libDTTPlugin.dylib'),
      \ '/usr/local/lib/libDTTPlugin.so',
      \ '/usr/lib/libDTTPlugin.so',
      \ '/opt/homebrew/lib/libDTTPlugin.dylib',
      \ ]
  endif

  for l:loc in l:locations
    if filereadable(l:loc)
      return l:loc
    endif
  endfor

  return ''
endfunction

" Register with ALE
function! s:RegisterALE() abort
  if !exists('g:ale_linters')
    let g:ale_linters = {}
  endif

  call ale#linter#Define('cpp', {
    \ 'name': 'dtt',
    \ 'executable': g:dtt_clang_path,
    \ 'command': function('s:GetCommand'),
    \ 'callback': 'ale#handlers#gcc#HandleGCCFormat',
    \ 'output_stream': 'stderr',
    \ })

  call ale#linter#Define('c', {
    \ 'name': 'dtt',
    \ 'executable': g:dtt_clang_path,
    \ 'command': function('s:GetCommand'),
    \ 'callback': 'ale#handlers#gcc#HandleGCCFormat',
    \ 'output_stream': 'stderr',
    \ })
endfunction

function! s:GetCommand(buffer) abort
  let l:plugin = s:FindPlugin()
  if empty(l:plugin)
    echoerr 'DTT: Plugin not found. Set g:dtt_plugin_path'
    return ''
  endif

  if s:IsWindows()
    " Windows requires -Xclang syntax for plugins
    return g:dtt_clang_path
      \ . ' -Xclang -load -Xclang ' . l:plugin
      \ . ' -Xclang -add-plugin -Xclang dtt'
      \ . ' -Xclang -plugin-arg-dtt -Xclang --dtt-mode=' . g:dtt_mode
      \ . ' -std=' . g:dtt_standard
      \ . ' -fsyntax-only %t'
  else
    return g:dtt_clang_path
      \ . ' -fplugin=' . l:plugin
      \ . ' -fplugin-arg-dtt---dtt-mode=' . g:dtt_mode
      \ . ' -std=' . g:dtt_standard
      \ . ' -fsyntax-only %t'
  endif
endfunction

" Auto-register if ALE is available
if exists('g:loaded_ale')
  call s:RegisterALE()
else
  augroup dtt_ale_init
    autocmd!
    autocmd User ALELintPre call s:RegisterALE()
  augroup END
endif
