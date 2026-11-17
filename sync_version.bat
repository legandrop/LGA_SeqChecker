@echo off
setlocal

set "SCRIPT=%~dp0tools\sync_version.py"
if not exist "%SCRIPT%" (
    echo ERROR: No se encontro "%SCRIPT%".
    exit /b 1
)

REM Solo el interprete embebido, con su ruta: `python` sin ruta es el alias de la Microsoft Store
REM o el Python de otra app que quedo en el PATH, nunca el de este repo. Primero el del repo y,
REM si este checkout no lo trae, el de LGA_FileManagerS3 clonado al lado. Desde un worktree fuera
REM de C:\Portable ninguno de los dos esta: se prueban tambien el del checkout principal (el padre
REM de la .git comun) y el LGA_FileManagerS3 a su lado.
set "MAIN_DIR="
for /f "delims=" %%G in ('git -C "%~dp0." rev-parse --path-format^=absolute --git-common-dir 2^>nul') do set "MAIN_DIR=%%G\.."
if defined MAIN_DIR set "MAIN_DIR=%MAIN_DIR:/=\%"
set "PY=%~dp0python_runtime\windows\python.exe"
if not exist "%PY%" set "PY=%~dp0..\LGA_FileManagerS3\python_runtime\windows\python.exe"
if not exist "%PY%" if defined MAIN_DIR set "PY=%MAIN_DIR%\python_runtime\windows\python.exe"
if not exist "%PY%" if defined MAIN_DIR set "PY=%MAIN_DIR%\..\LGA_FileManagerS3\python_runtime\windows\python.exe"
if not exist "%PY%" (
    echo ERROR: no se encontro el Python embebido: ni python_runtime\windows\python.exe de este
    echo        repo o de su checkout principal, ni el de LGA_FileManagerS3 al lado. No se usa
    echo        un Python del sistema.
    exit /b 1
)

"%PY%" "%SCRIPT%" %*
REM El codigo se pasa tal cual, sin `if errorlevel 1`: un crash de Python sale con un codigo
REM negativo (0xC0000005) y esa forma lo daba por bueno.
set "EXIT_CODE=%ERRORLEVEL%"
endlocal & exit /b %EXIT_CODE%
