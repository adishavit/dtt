set nomore
let g:dtt_auto_download = 0
let g:dtt_clang_path = $DTT_TEST_CLANG
let g:dtt_plugin_path = $DTT_TEST_PLUGIN
execute 'source ' . fnameescape($DTT_TEST_ROOT . '/ide-plugins/vim-dtt/plugin/dtt.vim')
let s:scripts = filter(getscriptinfo(), 'v:val.name =~# "plugin/dtt.vim$"')
let s:GetCommand = function('<SNR>' . s:scripts[0].sid . '_GetCommand')
let s:cmd = s:GetCommand(bufnr())
let s:cmd = substitute(s:cmd, '%t', shellescape('input file.cpp'), '')
let s:args = systemlist(s:cmd)
call assert_equal(0, v:shell_error, string(s:args))
call assert_equal(['-fplugin=' . g:dtt_plugin_path,
      \ '-fplugin-arg-dtt---dtt-mode=inform', '-std=c++20',
      \ '-fsyntax-only', 'input file.cpp'], s:args)
if !empty(v:errors)
  call writefile(v:errors, $DTT_TEST_ERRORS)
  cquit
endif
qa!
