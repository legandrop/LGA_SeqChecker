@echo off
REM Compilacion rapida para desarrollo con Ninja
REM Solo recompila archivos modificados — mucho mas rapido que compilar.bat

if "%1"=="--help" goto :show_help
if "%1"=="-h" goto :show_help

set FORCE_CLEAN=false
set NO_DEPLOY=false
set BUILD_TYPE=Debug
set PARALLEL_CORES=%NUMBER_OF_PROCESSORS%

:parse_args
if "%1"=="" goto :main
if "%1"=="--force-clean" ( set FORCE_CLEAN=true & shift & goto :parse_args )
if "%1"=="--no-deploy"   ( set NO_DEPLOY=true  & shift & goto :parse_args )
if "%1"=="--release"     ( set BUILD_TYPE=Release & shift & goto :parse_args )
if "%1"=="--parallel"    ( set PARALLEL_CORES=%2 & shift & shift & goto :parse_args )
shift & goto :parse_args

:show_help
echo Uso: %0 [opciones]
echo.
echo   --force-clean    Limpiar build antes de compilar
echo   --no-deploy      No copiar DLLs Qt (rapido si ya existen)
echo   --release        Compilar en modo Release
echo   --parallel N     Usar N nucleos
echo.
echo Ejemplos:
echo   compilar_dev.bat               Compilacion incremental normal
echo   compilar_dev.bat --no-deploy   Ultra-rapido (DLLs ya copiadas)
echo   compilar_dev.bat --force-clean Limpieza completa
exit /b 0

:main
taskkill /F /IM SeqChecker.exe 2>nul
if %ERRORLEVEL% EQU 0 ( echo Proceso SeqChecker terminado. & timeout /t 1 >nul )

REM Asegurarse de estar en la carpeta raiz del proyecto
cd /d "%~dp0"

REM Si el build fue configurado con otro generador (ej: MinGW Makefiles), forzar limpieza
set "CMAKE_GENERATOR="
if exist "build\CMakeCache.txt" (
    for /f "tokens=2* delims==" %%A in ('findstr /C:"CMAKE_GENERATOR:INTERNAL=" "build\CMakeCache.txt"') do set "CMAKE_GENERATOR=%%B"
    if not "%CMAKE_GENERATOR%"=="" if /I not "%CMAKE_GENERATOR%"=="Ninja" (
        echo CMake estaba configurado con "%CMAKE_GENERATOR%". Se forzara limpieza para usar Ninja.
        set FORCE_CLEAN=true
    )
)

set PATH=%PATH%;C:\Qt\6.5.3\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\Ninja;C:\Program Files\LLVM\bin

if not exist "C:\Qt\Tools\Ninja\ninja.exe" (
    echo ERROR: Ninja no encontrado en C:\Qt\Tools\Ninja\ninja.exe
    echo Por favor instala Ninja desde Qt Maintenance Tool
    exit /b 1
)

if not exist "C:\Program Files\LLVM\bin\ld.lld.exe" (
    echo ERROR: lld no encontrado en C:\Program Files\LLVM\bin\ld.lld.exe
    echo Por favor instala LLVM desde https://github.com/llvm/llvm-project/releases
    exit /b 1
)

if "%FORCE_CLEAN%"=="true" (
    echo Limpiando build anterior...
    rmdir /s /q build 2>nul
)

if not exist build mkdir build
cd build

if not exist "CMakeCache.txt" goto :configure
if "%FORCE_CLEAN%"=="true" goto :configure
findstr /C:"stack,16777216" CMakeCache.txt >nul 2>&1
if errorlevel 1 (
    echo Configuracion antigua detectada, reconfigurando para usar lld...
    goto :configure
)
echo CMake ya configurado, reutilizando...
goto :compile

:configure
echo Configurando CMake con Ninja...
cmake .. -G "Ninja" -DCMAKE_PREFIX_PATH="C:/Qt/6.5.3/mingw_64" -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
    -DCMAKE_CXX_FLAGS_DEBUG="-g -O0 -Wno-unused-parameter" ^
    -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=lld -Wl,--stack,16777216" ^
    -DCMAKE_SHARED_LINKER_FLAGS="-fuse-ld=lld"
if %ERRORLEVEL% neq 0 ( echo Error CMake & cd .. & exit /b 1 )

:compile
echo Compilando con Ninja (%PARALLEL_CORES% nucleos)...
ninja -j%PARALLEL_CORES%
if %ERRORLEVEL% neq 0 ( echo Error de compilacion & cd .. & exit /b 1 )
goto :after_compile

:after_compile
cd /d "%~dp0"

REM Copiar py_scr SIEMPRE (cambian frecuentemente)
echo Copiando scripts Python...
if not exist "build\py_scr" mkdir build\py_scr
xcopy /E /Y py_scr build\py_scr >nul 2>&1

if "%NO_DEPLOY%"=="true" goto :run

REM Copiar dependencias si no existen
if not exist "build\libgcc_s_seh-1.dll" (
    echo Copiando runtimes GCC...
    copy "C:\Qt\Tools\mingw1310_64\bin\libgcc_s_seh-1.dll" build\ >nul
    copy "C:\Qt\Tools\mingw1310_64\bin\libstdc++-6.dll"    build\ >nul
    copy "C:\Qt\Tools\mingw1310_64\bin\libwinpthread-1.dll" build\ >nul
)
if not exist "build\Qt6Core.dll" (
    echo Copiando Qt DLLs...
    C:\Qt\6.5.3\mingw_64\bin\windeployqt.exe --compiler-runtime --dir build build\SeqChecker.exe >nul
)

:run
echo.
echo Compilacion lista: %TIME%
echo Ejecutando SeqChecker...
cd /d "%~dp0build"
start SeqChecker.exe
cd /d "%~dp0"
