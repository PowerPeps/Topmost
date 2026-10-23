@echo off
setlocal
cd /d "%~dp0"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" ( echo [ERREUR] vswhere introuvable & exit /b 1 )
set "VSINSTALL="
for /f "usebackq tokens=*" %%i in (`^""%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath^"`) do set "VSINSTALL=%%i"
if not defined VSINSTALL ( echo [ERREUR] Visual Studio avec outils C++ introuvable & exit /b 1 )
set "VCVARSALL=%VSINSTALL%\VC\Auxiliary\Build\vcvarsall.bat"
if not exist "%VCVARSALL%" ( echo [ERREUR] vcvarsall introuvable & exit /b 1 )

del /q Topmost.exe Topmost32.exe Topmost.arm64.exe TopmostHook64.dll TopmostHook32.dll TopmostHookArm64.dll Setup.exe 2>nul
if exist obj rmdir /s /q obj

call :build amd64        TopmostHook64.dll     Topmost.exe        || exit /b 1
call :build amd64_x86    TopmostHook32.dll     Topmost32.exe      || exit /b 1

set "ARM64=aucun"
call :build amd64_arm64  TopmostHookArm64.dll  Topmost.arm64.exe
if errorlevel 1 (
    del /q TopmostHookArm64.dll Topmost.arm64.exe 2>nul
    echo [ATTENTION] Outils ARM64 absents : Setup.exe refusera l'installation sur Windows ARM.
    echo             Composant Visual Studio requis : "MSVC ... C++ ARM64/ARM64EC build tools".
) else (
    set "ARM64=Topmost.arm64.exe"
)

set "ISCC="
for %%p in ("%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" "%ProgramFiles%\Inno Setup 6\ISCC.exe") do if exist "%%~p" set "ISCC=%%~p"
if not defined ISCC (
    where ISCC.exe >nul 2>&1 && set "ISCC=ISCC.exe"
)
if not defined ISCC (
    echo [ATTENTION] Inno Setup introuvable - binaires construits, Setup.exe non genere.
    echo Installez Inno Setup 6 : https://jrsoftware.org/isinfo.php
    exit /b 0
)
"%ISCC%" /Qp installer.iss
if errorlevel 1 ( echo [ERREUR] Compilation de l'installeur echouee & exit /b 1 )
echo.
echo OK : Topmost.exe (x64) + Topmost32.exe (x86) + ARM64=%ARM64% + Setup.exe
exit /b 0

:build
echo [BUILD] %~2 + %~3  (%~1)
if not exist "obj\%~1" mkdir "obj\%~1"
> "obj\%~1\go.bat" echo @echo off
>>"obj\%~1\go.bat" echo call "%VCVARSALL%" %~1 ^>nul 2^>^&1
>>"obj\%~1\go.bat" echo if errorlevel 1 exit /b 1
>>"obj\%~1\go.bat" echo rc /nologo /fo obj\%~1\Hook.res Hook.rc
>>"obj\%~1\go.bat" echo if errorlevel 1 exit /b 1
>>"obj\%~1\go.bat" echo rc /nologo /fo obj\%~1\Topmost.res Topmost.rc
>>"obj\%~1\go.bat" echo if errorlevel 1 exit /b 1
>>"obj\%~1\go.bat" echo cl /nologo /utf-8 /LD /MT /O2 /W3 /DUNICODE /D_UNICODE /Foobj\%~1\ Hook.cpp obj\%~1\Hook.res /Fe%~2 /link /DEF:Hook.def /IMPLIB:obj\%~1\Hook.lib
>>"obj\%~1\go.bat" echo if errorlevel 1 exit /b 1
>>"obj\%~1\go.bat" echo cl /nologo /utf-8 /MT /EHsc /O2 /W3 /DUNICODE /D_UNICODE /Foobj\%~1\ Topmost.cpp obj\%~1\Topmost.res /Fe%~3 /link /SUBSYSTEM:WINDOWS /MANIFEST:NO
cmd /c "obj\%~1\go.bat"
exit /b %errorlevel%
