@echo off
chcp 65001 >nul
REM Motor unico de compilacion para desarrollo Windows (Ninja + lld, incremental).
REM Se abastece solo: verifica -> copia -> windeployqt (ultimo recurso) -> verifica -> aborta.

set FORCE_CLEAN=false
set NO_DEPLOY=false
set NO_RUN=false
set BUILD_TYPE=Debug
set SHOW_HELP=false
set WAIT_FOR_APP=false
set PARALLEL_CORES=%NUMBER_OF_PROCESSORS%
REM La carpeta del script se toma ANTES de parsear: `shift` (sin /1) corre tambien el parametro
REM cero, asi que despues del parseo "~dp0" ya no da la carpeta del script sino la actual.
set "APP_ROOT=%~dp0"

:parse_args
if "%1"=="" goto after_args
if "%1"=="--force-clean" ( set "FORCE_CLEAN=true" & shift & goto parse_args )
if "%1"=="--no-deploy"   ( set "NO_DEPLOY=true" & shift & goto parse_args )
if "%1"=="--no-run"      ( set "NO_RUN=true" & shift & goto parse_args )
if "%1"=="--wait"        ( set "WAIT_FOR_APP=true" & shift & goto parse_args )
if "%1"=="--release"     ( set "BUILD_TYPE=Release" & shift & goto parse_args )
if "%1"=="--parallel"    ( set "PARALLEL_CORES=%2" & shift & shift & goto parse_args )
if "%1"=="--help" ( set "SHOW_HELP=true" & shift & goto parse_args )
if "%1"=="-h"     ( set "SHOW_HELP=true" & shift & goto parse_args )
shift
goto parse_args

:after_args
REM `call` + `goto main` hacia que --help COMPILARA el proyecto entero: la
REM subrutina retornaba y la ejecucion seguia de largo. Con `goto` no vuelve.
if "%SHOW_HELP%"=="true" goto :show_help
goto :main

:show_help
echo Uso: %0 [opciones]
echo.
echo Opciones:
echo   --wait           Dejar la app en foreground: se ven su stdout/stderr y su
echo                    codigo de salida. Por defecto se lanza en background y el
echo                    script termina enseguida.
echo   --force-clean    Limpiar build completamente antes de compilar
echo   --no-deploy      No verificar/copiar dependencias de runtime (rapido si ya estan)
echo   --no-run         Compilar y validar sin abrir SeqChecker
echo   --release        Compilar en modo Release
echo   --parallel N     Usar N nucleos para compilacion paralela (default: todos disponibles)
echo.
echo Ejemplos:
echo   %0
echo   %0 --wait
echo   %0 --force-clean --parallel 8
exit /b 0

:main
cd /d "%APP_ROOT%"

REM Cerrar SOLO la copia que corre desde build\ de ESTE repo, para que el linker no choque con el
REM exe bloqueado. Antes era "taskkill /F /IM", que cerraba tambien la app instalada con la que se
REM estaba trabajando. Ver tools\close_by_path.ps1. Sale con 2 solo si rechazo los parametros.
powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%APP_ROOT%tools\close_by_path.ps1" -ExeName SeqChecker.exe -ExactPath "%APP_ROOT%build\SeqChecker.exe"
if %ERRORLEVEL% equ 2 ( echo Error: close_by_path rechazo los parametros & exit /b 1 )
ping -n 2 127.0.0.1 >nul

echo Compilacion rapida para desarrollo Windows con Ninja (usando %PARALLEL_CORES% nucleos)

REM Un `build/` configurado con OTRO generador hace que CMake aborte con "generator
REM does not match the generator used previously". La lectura del generador cacheado
REM va en una subrutina y la comparacion afuera: cmd.exe expande los %VAR% de un
REM bloque `if (...)` al PARSEARLO, asi que escribir y leer la misma variable dentro
REM del mismo bloque siempre lee el valor viejo.
set "CACHED_GENERATOR="
if exist "build\CMakeCache.txt" call :read_generator
if not "%CACHED_GENERATOR%"=="" if /I not "%CACHED_GENERATOR%"=="Ninja" (
    echo El arbol estaba configurado con "%CACHED_GENERATOR%" y este script usa Ninja.
    echo Se limpia build\ para reconfigurarlo.
    set FORCE_CLEAN=true
)

if "%FORCE_CLEAN%"=="true" (
    echo Limpiando build anterior...
    rmdir /s /q build 2>nul
)

REM Rutas de las toolchains, centralizadas para no repetir el literal en cada copia.
set "QT_DIR=C:\Qt\6.5.3\mingw_64"
set "MINGW_BIN=C:\Qt\Tools\mingw1310_64\bin"

set PATH=%PATH%;%QT_DIR%\bin;%MINGW_BIN%;C:\Qt\Tools\Ninja;C:\Program Files\LLVM\bin

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

if not exist build mkdir build
cd build

echo Configurando CMake con Ninja...
cmake .. -G "Ninja" -DCMAKE_PREFIX_PATH="C:/Qt/6.5.3/mingw_64" -DCMAKE_BUILD_TYPE=%BUILD_TYPE% -DCMAKE_CXX_FLAGS_DEBUG="-g -O0 -Wno-unused-parameter" -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=lld -Wl,--stack,16777216" -DCMAKE_SHARED_LINKER_FLAGS="-fuse-ld=lld"
if %ERRORLEVEL% neq 0 (
    echo Error en configuracion CMake
    cd ..
    exit /b 1
)

echo Compilando con %PARALLEL_CORES% nucleos usando Ninja...
ninja -j%PARALLEL_CORES%
if %ERRORLEVEL% neq 0 (
    echo Error en compilacion
    cd ..
    exit /b 1
)

cd ..

REM py_scr cambia frecuentemente: se copia siempre, no detras de un centinela.
echo Copiando scripts Python...
if not exist "build\py_scr" mkdir build\py_scr
xcopy /E /Y py_scr build\py_scr >nul 2>&1

if not exist "build\SeqChecker.exe" (
    echo ERROR: CMake no genero build\SeqChecker.exe.
    exit /b 1
)

if "%NO_DEPLOY%"=="true" goto :after_deps

REM ============================================================
REM  DEPENDENCIAS DE RUNTIME: verificar -> reparar -> verificar
REM
REM  El arbol de build es incremental y nadie lo limpia, asi que en la corrida
REM  normal ya esta todo puesto y esto son N chequeos `if not exist`: no cuesta
REM  nada. windeployqt se llama SOLO cuando falta algo.
REM
REM  Lista derivada de find_package(Qt6 COMPONENTS Core Gui Widgets Concurrent
REM  Svg) en CMakeLists.txt, NO de lo que haya en build\: el arbol tenia
REM  Qt6Network.dll, los backends TLS y varios imageformats [qgif, qicns, qico,
REM  qtga, qtiff, qwbmp, qwebp] como residuo de un windeployqt sin acotar. La
REM  app no linkea Qt6::Network ni usa QNetworkAccessManager/QSslSocket, y su
REM  unico recurso de imagen es un SVG [resources/resources.qrc]: nada de eso
REM  es una dependencia real.
REM
REM  SI incluye iconengines\qsvgicon.dll: sin el, QIcon sobre SVG cae al plugin
REM  de imageformats y rasteriza al tamano intrinseco en vez de escalar.
REM ============================================================
set "DEP_LIST=libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll Qt6Core.dll Qt6Gui.dll Qt6Widgets.dll Qt6Concurrent.dll Qt6Svg.dll platforms\qwindows.dll imageformats\qsvg.dll iconengines\qsvgicon.dll"

echo Verificando dependencias de runtime...
set DEPS_MISSING=false
for %%D in (%DEP_LIST%) do call :check_dep "%%D"

if "%DEPS_MISSING%"=="true" call :copy_missing_deps
if "%DEPS_MISSING%"=="true" (
    set DEPS_MISSING=false
    for %%D in (%DEP_LIST%) do call :check_dep "%%D"
)

REM Ultimo recurso: windeployqt, acotado para no dejar traducciones ni OpenGL
REM por software que la app no usa.
if "%DEPS_MISSING%"=="true" call :run_windeployqt
if "%DEPS_MISSING%"=="true" (
    set DEPS_MISSING=false
    for %%D in (%DEP_LIST%) do call :check_dep "%%D"
)

if "%DEPS_MISSING%"=="true" (
    echo.
    echo ERROR: faltan dependencias de runtime y no se pudieron reparar.
    echo        Verifica que Qt 6.5.3 mingw_64 este instalado en "%QT_DIR%".
    exit /b 1
)
echo Dependencias de runtime verificadas.

:after_deps

echo.
echo Compilacion lista: %TIME%

if "%NO_RUN%"=="true" (
    echo Ejecucion omitida ^(--no-run^).
    exit /b 0
)

echo Ejecutando SeqChecker...
cd build
if "%WAIT_FOR_APP%"=="true" (
    echo === INICIO DE EJECUCION [--wait] ===
    REM `.\` no es cosmetico: con NoDefaultCurrentDirectoryInExePath=1 cmd.exe NO
    REM busca ejecutables en el directorio actual y el nombre pelado devuelve 9009.
    .\SeqChecker.exe
) else (
    start SeqChecker.exe
)
REM El codigo de salida se lee ACA, fuera del bloque: adentro, cmd.exe lo habria
REM expandido al parsear, o sea antes de que la app corriera.
call :report_exit %%ERRORLEVEL%%
cd ..

if "%WAIT_FOR_APP%"=="true" exit /b %APP_EXIT_CODE%
exit /b 0

REM ============================================================
REM  Subrutinas
REM
REM  Van en subrutinas y no inline a proposito: cmd.exe expande los %VAR% de un
REM  bloque `if (...)` al PARSEARLO, no al ejecutarlo, asi que una variable que
REM  se escribe y se lee dentro del mismo bloque lee siempre el valor viejo. Un
REM  `call` reparsea el cuerpo en cada invocacion y esquiva el problema sin
REM  necesidad de `setlocal EnableDelayedExpansion`.
REM ============================================================

:report_exit
set "APP_EXIT_CODE=%~1"
if not "%WAIT_FOR_APP%"=="true" goto :eof
echo.
echo === FIN DE EJECUCION - codigo de salida: %~1 ===
if "%~1"=="-1073741819" echo DIAGNOSTICO: acceso a memoria invalido [equivale a un segfault]
if "%~1"=="-1073741571" echo DIAGNOSTICO: desbordamiento del stack [tipicamente recursion infinita]
if "%~1"=="-1073740791" echo DIAGNOSTICO: desbordamiento de buffer en el stack
goto :eof

:read_generator
for /f "tokens=2 delims==" %%A in ('findstr /C:"CMAKE_GENERATOR:INTERNAL=" "build\CMakeCache.txt"') do set "CACHED_GENERATOR=%%A"
goto :eof

:check_dep
if not exist "build\%~1" (
    echo    [falta] build\%~1
    set DEPS_MISSING=true
)
goto :eof

:copy_missing_deps
echo.
echo Faltan dependencias de runtime. Copiandolas...

REM Sin `2>nul`: una copia que falla tiene que verse.
call :copy_dep "%MINGW_BIN%" "" libgcc_s_seh-1.dll
call :copy_dep "%MINGW_BIN%" "" libstdc++-6.dll
call :copy_dep "%MINGW_BIN%" "" libwinpthread-1.dll
call :copy_dep "%QT_DIR%\bin" "" Qt6Core.dll
call :copy_dep "%QT_DIR%\bin" "" Qt6Gui.dll
call :copy_dep "%QT_DIR%\bin" "" Qt6Widgets.dll
call :copy_dep "%QT_DIR%\bin" "" Qt6Concurrent.dll
call :copy_dep "%QT_DIR%\bin" "" Qt6Svg.dll
call :copy_dep "%QT_DIR%\plugins\platforms" "platforms" qwindows.dll
call :copy_dep "%QT_DIR%\plugins\imageformats" "imageformats" qsvg.dll
REM OJO: qsvgicon.dll vive en plugins\iconengines, NO en plugins\imageformats.
call :copy_dep "%QT_DIR%\plugins\iconengines" "iconengines" qsvgicon.dll
goto :eof

:run_windeployqt
set "WINDEPLOYQT=%QT_DIR%\bin\windeployqt.exe"
if not exist "%WINDEPLOYQT%" (
    for /f "delims=" %%W in ('where windeployqt.exe 2^>nul') do set "WINDEPLOYQT=%%W"
)

if not exist "%WINDEPLOYQT%" (
    echo ADVERTENCIA: no se encontro windeployqt.exe.
    goto :eof
)

echo Todavia faltan dependencias: probando con windeployqt...
REM Acotado: sin estos flags deja traducciones que la app no usa [UI solo en
REM ingles], OpenGL por software y el compilador de D3D.
"%WINDEPLOYQT%" --compiler-runtime --no-translations --no-opengl-sw --no-system-d3d-compiler --dir build build\SeqChecker.exe
if errorlevel 1 echo ADVERTENCIA: windeployqt devolvio error.
goto :eof

:copy_dep
set "DEP_DEST=build"
if not "%~2"=="" set "DEP_DEST=build\%~2"
if exist "%DEP_DEST%\%~3" goto :eof
if not exist "%DEP_DEST%" mkdir "%DEP_DEST%"
if not exist "%~1\%~3" (
    echo    ERROR: no existe el origen "%~1\%~3"
    goto :eof
)
copy /Y "%~1\%~3" "%DEP_DEST%\" >nul
if errorlevel 1 (
    echo    ERROR: fallo la copia de "%~1\%~3"
) else (
    echo    [ok] %~3
)
goto :eof
