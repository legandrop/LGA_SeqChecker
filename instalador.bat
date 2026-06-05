@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"
set "SCRIPT_DIR=%~dp0"
set "INSTALLER_DIR=%SCRIPT_DIR%installer"
set "GITHUB_REPO=legandrop/LGA_SeqChecker"
set "GITHUB_READY=true"
set "GH_CMD="
set "GITHUB_LOCAL_ONLY_CONFIRMED=false"
set "CURRENT_BRANCH="

echo.
echo ============================================================
echo  Preflight checks de Git/GitHub (no fatal)
echo ============================================================
echo.

where git >nul 2>nul
if !errorlevel! EQU 0 (
    echo OK: Git disponible en PATH.
) else (
    echo AVISO: Git no esta disponible en PATH. Se generara solo el instalador local.
    set "GITHUB_READY=false"
    goto :GIT_PREFLIGHT_DONE
)

git rev-parse --show-toplevel >nul 2>nul
if !errorlevel! EQU 0 (
    echo OK: Repositorio Git valido.
) else (
    echo AVISO: Esta carpeta no es un repositorio Git. Se generara solo el instalador local.
    set "GITHUB_READY=false"
    goto :GIT_PREFLIGHT_DONE
)

for /f "tokens=*" %%B in ('git rev-parse --abbrev-ref HEAD 2^>nul') do set "CURRENT_BRANCH=%%B"
if "!CURRENT_BRANCH!"=="" (
    echo AVISO: No se pudo detectar el branch activo. Solo instalador local.
    set "GITHUB_READY=false"
    goto :GIT_PREFLIGHT_DONE
) else (
    echo OK: Branch activo detectado: !CURRENT_BRANCH!
)

if /i "!CURRENT_BRANCH!" NEQ "main" (
    echo AVISO: El branch activo es '!CURRENT_BRANCH!' [no 'main']. Solo instalador local.
    set "GITHUB_READY=false"
    goto :GIT_PREFLIGHT_DONE
)

set "HAS_PREEXISTING_CHANGES=false"
for /f "tokens=*" %%S in ('git status --porcelain 2^>nul') do set "HAS_PREEXISTING_CHANGES=true"
if "!HAS_PREEXISTING_CHANGES!"=="true" (
    echo AVISO: Hay cambios sin commitear. Se generara el instalador local sin publicar release.
    git status --short
    set "GITHUB_READY=false"
    goto :GIT_PREFLIGHT_DONE
) else (
    echo OK: El repositorio esta limpio.
)

where gh >nul 2>nul
if !errorlevel! EQU 0 (
    set "GH_CMD=gh"
) else (
    if exist "C:\Program Files\GitHub CLI\gh.exe" set "GH_CMD=C:\Program Files\GitHub CLI\gh.exe"
)

if "!GH_CMD!"=="" (
    echo AVISO: GitHub CLI [gh] no esta instalado.
    set "GITHUB_READY=false"
) else (
    echo OK: GitHub CLI encontrado.
    "!GH_CMD!" auth status >nul 2>nul
    if !errorlevel! NEQ 0 (
        echo AVISO: GitHub CLI no esta autenticado.
        set "GITHUB_READY=false"
    ) else (
        echo OK: GitHub CLI autenticado.
        git ls-remote --exit-code origin HEAD >nul 2>nul
        if !errorlevel! NEQ 0 (
            echo AVISO: No se pudo acceder al remoto origin.
            set "GITHUB_READY=false"
        ) else (
            echo OK: Acceso al remoto origin confirmado.
        )
    )
)

:GIT_PREFLIGHT_DONE
if /i "!GITHUB_READY!" NEQ "true" set "GITHUB_LOCAL_ONLY_CONFIRMED=true"

echo.
echo Creando instalador de LGA SeqChecker...

REM Cerrar proceso activo para evitar bloqueos durante deploy/installer
taskkill /F /IM SeqChecker.exe 2>nul

REM Ejecutar deploy automaticamente (sin abrir la app al finalizar)
echo Ejecutando deploy.bat --no-run...
call "%~dp0deploy.bat" --no-run
if %ERRORLEVEL% neq 0 (
    echo ERROR: Fallo deploy.bat. Abortando creacion de instalador.
    exit /b 1
)

REM Buscar Inno Setup
set ISCC=""
if exist "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" set ISCC="C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
if exist "C:\Program Files\Inno Setup 6\ISCC.exe" set ISCC="C:\Program Files\Inno Setup 6\ISCC.exe"

if %ISCC%=="" (
    echo ERROR: No se encontro Inno Setup 6.
    echo Descargar desde: https://jrsoftware.org/isdl.php
    exit /b 1
)

REM Leer version (max CMake / ChangeLog)
echo Sincronizando version (max CMake / ChangeLog)...
call "%~dp0sync_version.bat"
if %ERRORLEVEL% neq 0 (
    echo ERROR: Fallo la sincronizacion de version.
    exit /b 1
)

if not exist VERSION (
    echo ERROR: No se encontro el archivo VERSION.
    exit /b 1
)

set /p VERSION=<VERSION
if "%VERSION%"=="" (
    echo ERROR: El archivo VERSION esta vacio.
    exit /b 1
)

if /i "!GITHUB_READY!"=="true" (
    git rev-parse -q --verify "refs/tags/v%VERSION%" >nul 2>nul
    if !errorlevel! EQU 0 (
        echo AVISO: El tag local v%VERSION% ya existe.
        set "GITHUB_READY=false"
    ) else (
        git ls-remote --exit-code --tags origin "refs/tags/v%VERSION%" >nul 2>nul
        if !errorlevel! EQU 0 (
            echo AVISO: El tag remoto v%VERSION% ya existe.
            set "GITHUB_READY=false"
        ) else (
            echo OK: El tag v%VERSION% esta disponible.
        )
    )
)

REM Configurar hilos de compresion para Inno Setup (LZMA2)
set "LZMA_THREADS=%NUMBER_OF_PROCESSORS%"
set "LZMA_THREADS_SOURCE=auto"
if defined LGA_INSTALLER_THREADS (
    set "LZMA_THREADS=%LGA_INSTALLER_THREADS%"
    set "LZMA_THREADS_SOURCE=env"
)
set /a _LZMA_THREADS_NUM=%LZMA_THREADS% >nul 2>nul
if errorlevel 1 set "_LZMA_THREADS_NUM=4"
if %_LZMA_THREADS_NUM% LSS 1 set "_LZMA_THREADS_NUM=1"
if /I "%LZMA_THREADS_SOURCE%"=="auto" (
    if %_LZMA_THREADS_NUM% GTR 6 set "_LZMA_THREADS_NUM=6"
) else (
    if %_LZMA_THREADS_NUM% GTR 16 set "_LZMA_THREADS_NUM=16"
)
echo Compresion Inno Setup en modo multi-core: %_LZMA_THREADS_NUM% hilos (LZMA2).

REM Generar archivo .iss
(
echo #define MyAppName "LGA SeqChecker"
echo #define MyAppVersion "%VERSION%"
echo #define MyAppPublisher "LGA"
echo #define MyAppExeName "SeqChecker.exe"
echo #define MyAppOutputDir "installer"
echo.
echo [Setup]
echo AppId={{B7E2B4A1-9C3D-4F6E-8A2B-1D5E7F0C3A99}
echo AppName={#MyAppName}
echo AppVersion={#MyAppVersion}
echo AppPublisher={#MyAppPublisher}
echo DefaultDirName=C:\Portable\LGA\SeqChecker
echo DefaultGroupName={#MyAppName}
echo OutputDir={#MyAppOutputDir}
echo OutputBaseFilename=LGA_SeqChecker_Setup_v{#MyAppVersion}
echo PrivilegesRequired=lowest
echo UsePreviousAppDir=no
echo DirExistsWarning=no
echo Compression=lzma2
echo LZMANumBlockThreads=%_LZMA_THREADS_NUM%
echo SolidCompression=yes
echo WizardStyle=modern
echo.
echo [Languages]
echo Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"
echo.
echo [Tasks]
echo Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
echo.
echo [Files]
echo Source: "deploy\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
echo.
echo [Icons]
echo Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
echo Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon
echo.
echo [Run]
echo Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent
) > SeqChecker_installer.iss

if not exist "%INSTALLER_DIR%" mkdir "%INSTALLER_DIR%"

set "OUTPUT_EXE=%INSTALLER_DIR%\LGA_SeqChecker_Setup_v%VERSION%.exe"
if exist "%OUTPUT_EXE%" del /F /Q "%OUTPUT_EXE%" >nul 2>nul

%ISCC% SeqChecker_installer.iss
if %ERRORLEVEL% neq 0 (
    echo Aviso: primer intento de compilacion fallo. Reintentando en 5 segundos...
    ping 127.0.0.1 -n 6 >nul
    %ISCC% SeqChecker_installer.iss
)
if %ERRORLEVEL% neq 0 (
    echo ERROR: Fallo la compilacion del instalador con Inno Setup.
    exit /b 1
)

echo.
echo Instalador creado: %OUTPUT_EXE%

if /i "!GITHUB_READY!" NEQ "true" goto :AFTER_GITHUB_RELEASE

echo.
echo ============================================================
echo  Commit y release opcional
echo ============================================================
echo.

set "RELEASE_ALLOWED=true"
set "COMMIT_CREATED=false"
set "HAS_INSTALLER_CHANGES=false"
for /f "tokens=*" %%S in ('git status --porcelain 2^>nul') do set "HAS_INSTALLER_CHANGES=true"

if "!HAS_INSTALLER_CHANGES!"=="true" (
    echo Cambios detectados despues de crear el instalador:
    git status --short
    echo.
    choice /C YN /M "Desea commitear estos cambios como installer_v%VERSION%?"
    if !errorlevel! EQU 1 (
        echo Haciendo commit de cambios...
        git add -A
        git commit -m "installer_v%VERSION%"
        if !errorlevel! NEQ 0 (
            echo AVISO: git commit retorno codigo !errorlevel!.
            choice /C YN /M "Desea continuar con la release sin un commit nuevo?"
            if !errorlevel! NEQ 1 set "RELEASE_ALLOWED=false"
        ) else (
            set "COMMIT_CREATED=true"
        )
    ) else (
        echo Commit omitido por el usuario.
        set "RELEASE_ALLOWED=false"
    )
) else (
    echo No hay cambios nuevos para commitear.
)

if /i "!COMMIT_CREATED!"=="true" (
    echo Haciendo push a origin/!CURRENT_BRANCH!...
    git push origin "!CURRENT_BRANCH!"
    if !errorlevel! NEQ 0 set "RELEASE_ALLOWED=false"
)

if /i "!RELEASE_ALLOWED!" NEQ "true" goto :AFTER_GITHUB_RELEASE

echo.
choice /C YN /M "Desea subir el instalador como release v%VERSION% a GitHub?"
if !errorlevel! NEQ 1 goto :AFTER_GITHUB_RELEASE

if not exist "%OUTPUT_EXE%" (
    echo ERROR: No se encontro el instalador: %OUTPUT_EXE%
    goto :AFTER_GITHUB_RELEASE
)

echo Creando tag v%VERSION%...
git tag -a "v%VERSION%" -m "Release v%VERSION%"
if !errorlevel! NEQ 0 goto :AFTER_GITHUB_RELEASE

echo Haciendo push del tag v%VERSION%...
git push origin "v%VERSION%"
if !errorlevel! NEQ 0 goto :AFTER_GITHUB_RELEASE

echo Creando release en GitHub...
"!GH_CMD!" release create "v%VERSION%" "%OUTPUT_EXE%" --repo "%GITHUB_REPO%" --title "v%VERSION%" --notes "Release v%VERSION%"
if !errorlevel! NEQ 0 (
    echo ERROR: No se pudo crear la release en GitHub.
    goto :AFTER_GITHUB_RELEASE
)

echo.
echo ============================================================
echo  Release v%VERSION% publicada exitosamente en GitHub!
echo  https://github.com/%GITHUB_REPO%/releases/tag/v%VERSION%
echo ============================================================

:AFTER_GITHUB_RELEASE
echo.

choice /C YN /M "Desea ejecutar el instalador ahora mismo?"
if %ERRORLEVEL%==1 (
    echo Ejecutando el instalador...
    start "" "%OUTPUT_EXE%"
) else (
    echo Instalador no ejecutado.
)

choice /C YN /M "Desea revelar el instalador en Windows Explorer?"
if %ERRORLEVEL%==1 (
    explorer /select,"%OUTPUT_EXE%"
) else (
    echo No se abrio Windows Explorer.
)

endlocal
