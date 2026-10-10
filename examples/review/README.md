# Editor regression checks

Run from the repository root after building `build/libDTTPlugin.so` with Clang 18.
The full analyzer suite runs separately with CTest, in the background.

- VS Code: `npm --prefix ide-plugins/vscode-dtt test`
- Neovim: `nvim --headless -u NONE -l examples/review/nvim_editor.lua`
- Emacs with Flycheck on `load-path`: `emacs --batch -l examples/review/emacs_editor.el`
- CLion: `bash ide-plugins/clion-dtt/gradlew -p ide-plugins/clion-dtt compileKotlin`,
  then `python3 examples/review/run_clion_checks.py`. The standalone harness uses
  the cached CLion 2025.1 SDK and Kotlin 2.0.21 compiler from that build.
- Visual Studio runner on Linux: `dotnet run --project examples/review/vs_runner -- build/libDTTPlugin.so`.
  This runs the actual C# runner against Linux Clang; it does not load the VS IDE.
- Native Windows build and archive extraction: run
  `powershell.exe -NoProfile -File <Windows-repo-path>\examples\review\windows_editors.ps1`
  from WSL, or run the script directly in Windows PowerShell. It stages only the
  Visual Studio extension sources under `build/native-vs` so Windows and Linux
  intermediate files do not collide.

`vim_setup.vim` checks startup acceptance. Set `DTT_TEST_CLANG`, `DTT_TEST_PLUGIN`,
`DTT_TEST_EXPECT_READY` (`1` or `0`) and `DTT_TEST_ERRORS` (a writable log path),
then run `vim -Nu NONE -i NONE -n -es -S examples/review/vim_setup.vim`.
Use the working Clang/plugin pair, an invalid plugin file, and a compiler reporting
another LLVM major. Vim command escaping is also covered by CTest's
`review_small_regressions`.

These checks cover buffer snapshots, local includes, stale results, startup
compatibility and process handling. They do not establish native IDE installation,
marketplace publication, or a working Windows/macOS analyzer package.
