set nomore
let g:dtt_auto_download = 0
let g:dtt_clang_path = $DTT_TEST_CLANG
let g:dtt_plugin_path = $DTT_TEST_PLUGIN
source ide-plugins/vim-dtt/plugin/dtt.vim
let s:scripts = filter(getscriptinfo(), 'v:val.name =~# "plugin/dtt.vim$"')
let s:Ensure = function('<SNR>' . s:scripts[0].sid . '_EnsureBinaries')
call assert_equal(str2nr($DTT_TEST_EXPECT_READY), s:Ensure())
if !empty(v:errors)
  call writefile(v:errors, $DTT_TEST_ERRORS)
  cquit
endif
qa!
