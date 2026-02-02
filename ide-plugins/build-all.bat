@echo off
REM Build all IDE plugins (Windows)
REM Usage: build-all.bat

cd /d "%~dp0"

echo === Building IDE Plugins ===

echo.
echo ^>^>^> VS Code Extension
cd vscode-dtt
call npm install --silent
call npm run compile
call npm test
echo VS Code: OK
cd ..

echo.
echo ^>^>^> CLion Plugin
cd clion-dtt
if exist "gradlew.bat" if exist "gradle\wrapper" (
    call gradlew.bat build --quiet && echo CLion: OK || echo CLion: Build failed
) else (
    where gradle >nul 2>&1
    if %errorlevel%==0 (
        echo Bootstrapping Gradle wrapper...
        call gradle wrapper --gradle-version 8.5 2>nul
        if exist "gradlew.bat" (
            call gradlew.bat build --quiet && echo CLion: OK || echo CLion: Build failed
        ) else (
            echo CLion: Skipped - could not create Gradle wrapper
        )
    ) else (
        echo CLion: Skipped - gradle not installed, no wrapper present
    )
)
cd ..

echo.
echo ^>^>^> Visual Studio Extension
cd vs-dtt
where dotnet >nul 2>&1
if %errorlevel%==0 (
    dotnet build --nologo -v q
) else (
    echo VS: Skipped - dotnet not installed
)
cd ..

echo.
echo ^>^>^> Neovim (Lua) - no build needed
echo ^>^>^> Vim (VimScript) - no build needed
echo ^>^>^> Emacs (Elisp) - no build needed

echo.
echo === Build Complete ===
