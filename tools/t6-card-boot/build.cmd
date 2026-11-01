@echo off
setlocal
set "T6_CARD_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%T6_CARD_VSWHERE%" exit /b 1
for /f "usebackq tokens=*" %%i in (`"%T6_CARD_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "T6_CARD_VS=%%i"
if not defined T6_CARD_VS exit /b 1
call "%T6_CARD_VS%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0"
if not exist build mkdir build
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MD /DUNICODE /D_UNICODE /Fe:build\t6-card-boot.exe /Fo:build\t6_card_boot.obj t6_card_boot.cpp /link bcrypt.lib advapi32.lib shell32.lib ole32.lib
if errorlevel 1 exit /b 1
set "T6_CARD_TEST_FLAGS=/O2"
if "%~1"=="test-asan" set "T6_CARD_TEST_FLAGS=/Od /Zi /fsanitize=address"
cl /nologo /std:c++17 /EHsc /W4 /WX %T6_CARD_TEST_FLAGS% /MD /DUNICODE /D_UNICODE /Fd:build\card_tests.pdb /Fe:build\card-fixture-tests.exe /Fo:build\card_tests.obj card_tests.cpp
if errorlevel 1 exit /b 1
if "%~1"=="test" build\card-fixture-tests.exe
if "%~1"=="test-asan" build\card-fixture-tests.exe
exit /b %errorlevel%
