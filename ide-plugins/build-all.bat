@echo off
REM Build all IDE plugins (Windows)
REM Usage: build-all.bat

cd /d "%~dp0"

echo === Building IDE Plugins ===

echo.
echo ^>^>^> VS Code Extension
cd vscode-dtt
call npm install --silent
if %errorlevel% neq 0 (
    echo VS Code: npm install failed
    cd ..
    exit /b 1
)
call npm run compile
if %errorlevel% neq 0 (
    echo VS Code: compilation failed
    cd ..
    exit /b 1
)
call npm test
if %errorlevel% neq 0 (
    echo VS Code: tests failed
    cd ..
    exit /b 1
)
call npx vsce package --allow-missing-repository -o dtt.vsix 2>nul && echo VS Code: OK (dtt.vsix created) || echo VS Code: OK
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
    if %errorlevel% neq 0 (
        echo VS: Build failed
        cd ..
        exit /b 1
    )
    echo VS: OK
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
