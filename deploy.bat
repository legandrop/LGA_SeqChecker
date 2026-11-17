@echo off
setlocal
cd /d "%~dp0"
REM La carpeta del script se toma ANTES de parsear: `shift` corre tambien el parametro cero, y
REM despues del parseo "~dp0" ya no da la carpeta del script sino la actual.
set "APP_ROOT=%~dp0"

set "NO_RUN=false"
set "BUILD_DIR=build_deploy"
set "PARALLEL_CORES=%NUMBER_OF_PROCESSORS%"
set "BAD_ARG="

:parse_args
if "%~1"=="" goto after_args
if /I "%~1"=="--no-run" ( set "NO_RUN=true" & shift & goto parse_args )
set "BAD_ARG=%~1"

:after_args
REM Una opcion mal escrita corta ACA, antes de cerrar, compilar o borrar nada. Ignorarla hacia
REM que un --no-run con un error de tipeo terminara abriendo la app.
if defined BAD_ARG (
    echo ERROR: opcion desconocida: "%BAD_ARG%". Unica opcion: --no-run.
    echo No se cerro, no se compilo ni se borro nada.
    exit /b 1
)

REM Cerrar SOLO las copias que corren desde deploy\ y build_deploy\ de ESTE repo, ANTES de
REM compilar (el link fallaria con el exe de build_deploy\ corriendo) y de borrar deploy\ (con el
REM exe corriendo, el rmdir lo dejaba a medias). Antes era
REM "taskkill /F /IM", que cerraba tambien la app instalada. Ver tools\close_by_path.ps1.
powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%APP_ROOT%tools\close_by_path.ps1" -ExeName SeqChecker.exe -ExactPath "%APP_ROOT%deploy\SeqChecker.exe,%APP_ROOT%%BUILD_DIR%\SeqChecker.exe"
if %ERRORLEVEL% equ 2 ( echo Error: close_by_path rechazo los parametros & exit /b 1 )

echo Implementando LGA SeqChecker...

set PATH=%PATH%;C:\Qt\6.5.3\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\Ninja;C:\Program Files\LLVM\bin

if not exist "C:\Qt\Tools\Ninja\ninja.exe" (
    echo ERROR: Ninja no encontrado en C:\Qt\Tools\Ninja\ninja.exe
    exit /b 1
)

if not exist "C:\Program Files\LLVM\bin\ld.lld.exe" (
    echo ERROR: lld no encontrado en C:\Program Files\LLVM\bin\ld.lld.exe
    echo Instale LLVM o ajuste deploy.bat para usar el linker GNU.
    exit /b 1
)

echo Configurando con CMake (modo Release)...
cmake -S . -B "%BUILD_DIR%" -G "Ninja" -DCMAKE_PREFIX_PATH="C:/Qt/6.5.3/mingw_64" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_DEBUG="-g -O0 -Wno-unused-parameter" -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=lld -Wl,--stack,16777216" -DCMAKE_SHARED_LINKER_FLAGS="-fuse-ld=lld"
if %ERRORLEVEL% neq 0 (
    echo Error en la configuracion de CMake.
    exit /b 1
)
echo Compilando (modo Release)...
cmake --build "%BUILD_DIR%" --config Release --parallel %PARALLEL_CORES%

if %ERRORLEVEL% neq 0 (
    echo Error en la compilacion.
    exit /b 1
)

REM deploy\ se borra recien ACA, con la compilacion ya hecha: si fallaba, antes quedaba sin
REM deploy anterior y sin deploy nuevo.
if not exist "%BUILD_DIR%\SeqChecker.exe" (
    echo ERROR: no se encontro %BUILD_DIR%\SeqChecker.exe. No se toca deploy\.
    exit /b 1
)
echo Limpiando carpeta de deploy anterior...
if exist deploy rmdir /S /Q deploy
if exist deploy (
    echo ERROR: no se pudo borrar deploy\ [hay archivos en uso]. No se genera el deploy.
    exit /b 1
)
mkdir deploy

copy /Y "%BUILD_DIR%\SeqChecker.exe" deploy\
if exist "%BUILD_DIR%\*.dll" copy /Y "%BUILD_DIR%\*.dll" deploy\

echo Copiando runtime Qt y MinGW...
xcopy /Y /Q "C:\Qt\6.5.3\mingw_64\bin\*.dll" deploy\ >nul
copy /Y "C:\Qt\Tools\mingw1310_64\bin\libgcc_s_seh-1.dll" deploy\ >nul
copy /Y "C:\Qt\Tools\mingw1310_64\bin\libstdc++-6.dll" deploy\ >nul
copy /Y "C:\Qt\Tools\mingw1310_64\bin\libwinpthread-1.dll" deploy\ >nul

echo Copiando plugins de Qt...
if not exist deploy\plugins mkdir deploy\plugins
xcopy /E /Y /Q /I "C:\Qt\6.5.3\mingw_64\plugins\*" deploy\plugins\ >nul

echo [Paths] > deploy\qt.conf
echo Plugins = plugins >> deploy\qt.conf

if not exist deploy\logs mkdir deploy\logs

REM Copiar python_runtime/windows
if not exist deploy\python_runtime\windows mkdir deploy\python_runtime\windows
xcopy /E /Y /I python_runtime\windows deploy\python_runtime\windows

REM Copiar py_scr (excluir __pycache__)
echo __pycache__ > exclude.txt
if not exist deploy\py_scr mkdir deploy\py_scr
xcopy /E /Y /I /EXCLUDE:exclude.txt py_scr deploy\py_scr
del exclude.txt

REM Copiar config
if not exist deploy\config mkdir deploy\config
xcopy /E /Y /I config deploy\config

REM Copiar thirdparty/win completo (OpenEXR: exrcheck/exrheader)
if not exist deploy\thirdparty\win mkdir deploy\thirdparty\win
xcopy /E /Y /I thirdparty\win deploy\thirdparty\win

echo.
echo Implementacion completada. App portable en carpeta 'deploy'.
echo.
if /I "%NO_RUN%"=="true" goto skip_run
REM La abre el Explorador de Windows, no esta consola: lanzada con start, la app queda dentro del
REM arbol de procesos de la terminal que corrio el script y se cierra con ella. explorer.exe no usa
REM el directorio actual, asi que la ruta va absoluta, por APP_ROOT. La app nace con cwd System32 y
REM sin el entorno de este script: deploy\ lleva sus DLL y su qt.conf al lado del exe. Si el exe no
REM esta no se llama a explorer.exe, que abriria una ventana de carpeta. explorer.exe siempre
REM devuelve 1: no se mira su codigo.
if not exist "%APP_ROOT%deploy\SeqChecker.exe" (
    echo ERROR: no existe "%APP_ROOT%deploy\SeqChecker.exe". No se abre nada.
    endlocal
    exit /b 1
)
explorer.exe "%APP_ROOT%deploy\SeqChecker.exe"
goto end_run

:skip_run
echo Omitiendo ejecucion de SeqChecker (--no-run).

:end_run
endlocal
exit /b 0
