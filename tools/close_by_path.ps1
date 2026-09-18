# close_by_path.ps1 -- rev 2 (2026-09-17)
#
# La copia CANONICA vive en LGA_Base_QT_C_Py\tools\close_by_path.ps1. Cada app lleva en su propio
# tools\ una copia IDENTICA, esta cabecera incluida (solo pueden cambiar los fines de linea), y no
# la modifica: los arreglos se hacen en la canonica y se vuelven a copiar. Asi esta cabecera es
# cierta en cualquier repo, y comparar una copia con la canonica es un diff sin excepciones.
#
# Cierra los procesos de UN ejecutable (-ExeName) cuya ruta REAL (Win32_Process.ExecutablePath)
# cae adentro de una carpeta (-Prefix) o es exactamente un archivo (-ExactPath). Nunca cierra por
# nombre de imagen: el nombre es solo el primer filtro de la consulta, la decision la toma la ruta.
#
# Por que existe: los scripts de build hacian `taskkill /F /IM <App>.exe`, que cierra TODAS las
# copias de la app, incluida la instalada con la que el usuario esta trabajando, y le corta a
# mitad lo que tenga en curso (transcodes, subidas, descargas). Referencias de las que sale:
# tools/kill_scenebuilder.ps1 de LGA_SceneBuilder (filtro por ExecutablePath) y el compilar.bat
# de LGA_FrameRev (ruta exacta del exe).
#
# USO (siempre con -File, desde un .bat o desde el [Code] de Inno):
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\close_by_path.ps1 ^
#       -ExeName App.exe -ExactPath "%APP_ROOT%build\App.exe"
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\close_by_path.ps1 ^
#       -ExeName App_py.exe -Prefix "%APP_ROOT%python_runtime" -Tree -TreeAllow "%APP_ROOT%thirdparty"
#   ... -Prefix "%APP_ROOT%build,%APP_ROOT%build-release" -DryRun
#
# Parametros:
#   -ExeName    OBLIGATORIO. Nombre de imagen, solo el nombre (`App.exe`), sin ruta ni comodines.
#               Es obligatorio para que un prefijo amplio no alcance a OTRO ejecutable que vive
#               abajo: el instalador del auto-update que corre desde {app}\updates, o el
#               python.exe de un runtime que comparten los hooks de varios repos.
#   -Prefix     Una o mas carpetas ABSOLUTAS separadas por coma (con o sin espacios alrededor).
#               Matchea todo exe que viva adentro, a cualquier profundidad.
#   -ExactPath  Una o mas rutas ABSOLUTAS de exe, separadas por coma. Matchea solo ese archivo.
#               Es el modo para el exe principal en compilar.bat: el linker necesita libre ESE
#               archivo y ningun otro. Su nombre de archivo tiene que ser -ExeName.
#               Hace falta -Prefix, -ExactPath o los dos.
#   -Tree       Cierra tambien los DESCENDIENTES de cada proceso que matcheo (hijos, nietos, y
#               tambien los lanzados desacoplados, con DETACHED_PROCESS: se encuentran por el
#               PID padre, no por la consola). Pero SOLO los que viven adentro de un -Prefix, son
#               un -ExactPath o viven adentro de un -TreeAllow. El resto se lista y se deja vivo,
#               y tampoco se baja por su rama: lo que cuelga de un proceso que se deja vivo es de
#               ese proceso.
#   -TreeAllow  Carpetas ABSOLUTAS de helpers conocidos que el backend lanza y que viven fuera
#               del -Prefix (ffmpeg en thirdparty\, redis, el python del runtime). Requiere -Tree.
#   -DryRun     Lista lo que cerraria y no cierra nada; con -Tree lista TODOS los descendientes
#               con su ruta y que haria con cada uno. Usarlo ANTES de correr por primera vez un
#               .bat real que se modifico.
#
# Por que -Tree NO cierra todo el arbol (rev 1 usaba `taskkill /T`, que si lo hace): un backend
# puede abrir OTRA app LGA como proceso aparte (abrir un archivo en el Player, lanzar el
# FileManager). Ese proceso es hijo del backend, pero es trabajo del usuario: `taskkill /T` lo
# cerraba, desacoplado o no. Dejar vivo un helper que no se nombro es la direccion segura: si
# retiene un archivo, el linker o Inno avisan "archivo en uso", y la linea del log dice cual es
# y donde vive para agregarlo a -TreeAllow.
#
# Guardas (un parametro invalido NO cierra nada; se avisa y se sale con 2):
#   - Rutas solo absolutas (`C:\...` o `\\servidor\recurso\...`). Se rechazan vacias, relativas
#     (`build`: es lo que queda de "%APP_ROOT%build" con APP_ROOT vacio), `C:carpeta`, comodines,
#     comillas sueltas, la raiz de una unidad y cualquier ruta con menos de DOS componentes debajo
#     de la unidad o del recurso compartido (`C:\Portable` se rechaza; `C:\Portable\App` no).
#   - -Prefix y -TreeAllow rechazan ademas las RAICES COMUNES de las instalaciones LGA
#     (`C:\Portable\LGA`, `C:\Program Files\LGA`, `C:\Program Files (x86)\LGA`,
#     `%LOCALAPPDATA%\LGA`, `%APPDATA%\LGA`) y cualquier carpeta que las contenga (el perfil del
#     usuario, `AppData`): con una variable mal armada, un prefijo asi alcanzaria a la instalada.
#     La carpeta de UNA app adentro (`C:\Portable\LGA\VideoDownloader`, el {app} de Inno) si vale.
#   - La comparacion de carpetas agrega el separador final: `...\PipeSync` NO matchea
#     `...\PipeSync_Client\...`, y `...\build` NO matchea `...\build-dump\...`.
#   - Un proceso sin ExecutablePath legible (otra sesion, sin privilegios) se deja vivo. Si la
#     consulta de procesos falla entera, no se cierra nada. Las dos son la direccion segura: lo
#     peor que pasa es que el linker o el instalador avisen que el archivo esta en uso.
#
# Salida: 0 si corrio (haya cerrado cero, uno o varios procesos, y aunque alguno no se haya podido
# cerrar: cada caso queda escrito con su PID y su ruta); 2 si rechazo los parametros. Cerrar cero
# procesos no es un error.
#
# -Prefix, -ExactPath y -TreeAllow son [string[]] y ademas se parten por coma a mano: invocado con
# -File, powershell.exe entrega "a,b" como UN solo string (bug medido en kill_scenebuilder.ps1).
# Tampoco se usa Mandatory: con -File y sin -NonInteractive, un parametro obligatorio que falta
# hace que PowerShell se quede esperando que alguien lo tipee, y el .bat queda colgado.

param(
    [string]   $ExeName = '',
    [string[]] $Prefix = @(),
    [string[]] $ExactPath = @(),
    [switch]   $Tree,
    [string[]] $TreeAllow = @(),
    [switch]   $DryRun
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2

$tag = 'close_by_path'

function Write-Line([string] $text) {
    Write-Host "${tag}: $text"
}

function Reject([string] $why) {
    Write-Line "RECHAZADO, no se cierra nada: $why"
    exit 2
}

# Parte una lista que puede venir como array, como "a,b" o como las dos cosas.
function Split-List([string[]] $items) {
    $out = @()
    foreach ($item in $items) {
        if ($null -eq $item) { continue }
        foreach ($part in ($item -split ',')) {
            $trimmed = $part.Trim()
            if ($trimmed -ne '') { $out += $trimmed }
        }
    }
    return ,$out
}

# Devuelve la ruta absoluta normalizada, sin separador final, o $null con el motivo en $script:why.
function Resolve-AbsolutePath([string] $raw) {
    $script:why = ''
    $p = $raw.Trim().Replace('/', '\')
    if ($p -eq '') { $script:why = 'ruta vacia'; return $null }
    if ($p -match '["*?<>|]') { $script:why = "caracteres invalidos en '$raw' (comillas o comodines)"; return $null }
    if ($p.StartsWith('\\?\') -or $p.StartsWith('\\.\')) { $script:why = "ruta de dispositivo no admitida: '$raw'"; return $null }

    $isDrive = $p -match '^[A-Za-z]:\\'
    $isUnc = $p -match '^\\\\[^\\]+\\[^\\]+'
    if (-not ($isDrive -or $isUnc)) { $script:why = "no es una ruta absoluta: '$raw'"; return $null }

    try {
        $full = [System.IO.Path]::GetFullPath($p)
    } catch {
        $script:why = "ruta invalida '$raw' ($($_.Exception.Message))"
        return $null
    }
    $full = $full.TrimEnd('\')

    # Componentes debajo de la unidad o del recurso compartido.
    $segments = @($full.Split('\') | Where-Object { $_ -ne '' })
    if ($isDrive) {
        $below = $segments.Count - 1          # C: + componentes
    } else {
        $below = $segments.Count - 2          # servidor + recurso + componentes
    }
    if ($below -lt 2) {
        $script:why = "ruta demasiado corta ('$full'): hacen falta al menos dos carpetas debajo de la unidad o del recurso"
        return $null
    }
    return $full
}

# Raices comunes de las instalaciones LGA. Un -Prefix o -TreeAllow que sea una de ellas, o que
# contenga a alguna, alcanzaria a las apps instaladas.
function Get-ProtectedRoots {
    $roots = @('C:\Portable\LGA')
    foreach ($base in @($env:ProgramFiles, ${env:ProgramFiles(x86)}, $env:ProgramW6432, $env:LOCALAPPDATA, $env:APPDATA)) {
        if ($base) { $roots += (Join-Path $base 'LGA') }
    }
    $out = @()
    foreach ($r in $roots) {
        try { $out += [System.IO.Path]::GetFullPath($r).TrimEnd('\') } catch { }
    }
    return ,$out
}

$protectedRoots = Get-ProtectedRoots

function Test-ProtectedFolder([string] $full) {
    foreach ($root in $protectedRoots) {
        if ($root.Equals($full, [System.StringComparison]::OrdinalIgnoreCase)) { return $root }
        if ($root.StartsWith($full + '\', [System.StringComparison]::OrdinalIgnoreCase)) { return $root }
    }
    return $null
}

function Resolve-FolderList([string[]] $raws, [string] $paramName) {
    $out = @()
    foreach ($raw in $raws) {
        $full = Resolve-AbsolutePath $raw
        if ($null -eq $full) { Reject "${paramName}: $script:why" }
        $hit = Test-ProtectedFolder $full
        if ($null -ne $hit) {
            Reject "${paramName}: '$full' es (o contiene) la raiz comun de las instalaciones LGA '$hit'; hay que pasar la carpeta de UNA app"
        }
        $out += ($full + '\')
    }
    return ,$out
}

# --- Validacion de parametros: nada se consulta ni se cierra hasta pasar todo esto ---

$ExeName = $ExeName.Trim()
if ($ExeName -eq '') { Reject 'falta -ExeName (obligatorio)' }
if ($ExeName -match '[\\/:"*?<>|'']') { Reject "-ExeName tiene que ser solo un nombre de archivo, sin ruta ni comodines: '$ExeName'" }
if (-not $ExeName.EndsWith('.exe', [System.StringComparison]::OrdinalIgnoreCase)) { Reject "-ExeName tiene que terminar en .exe: '$ExeName'" }

$prefixRaw = Split-List $Prefix
$exactRaw = Split-List $ExactPath
$allowRaw = Split-List $TreeAllow
if (($Prefix.Count -gt 0 -and $prefixRaw.Count -eq 0) -or
    ($ExactPath.Count -gt 0 -and $exactRaw.Count -eq 0) -or
    ($TreeAllow.Count -gt 0 -and $allowRaw.Count -eq 0)) {
    Reject 'se paso -Prefix, -ExactPath o -TreeAllow vacio (una variable de entorno sin definir?)'
}
if ($prefixRaw.Count -eq 0 -and $exactRaw.Count -eq 0) { Reject 'hace falta -Prefix o -ExactPath' }
if ($allowRaw.Count -gt 0 -and -not $Tree) { Reject '-TreeAllow solo tiene sentido con -Tree' }

$prefixes = Resolve-FolderList $prefixRaw '-Prefix'
$allows = Resolve-FolderList $allowRaw '-TreeAllow'

$exacts = @()
foreach ($raw in $exactRaw) {
    $full = Resolve-AbsolutePath $raw
    if ($null -eq $full) { Reject "-ExactPath: $script:why" }
    $leaf = [System.IO.Path]::GetFileName($full)
    if (-not $leaf.Equals($ExeName, [System.StringComparison]::OrdinalIgnoreCase)) {
        Reject "-ExactPath apunta a '$leaf' y -ExeName es '$ExeName'"
    }
    $exacts += $full
}

$mode = if ($DryRun) { 'DRY-RUN, no se cierra nada' } else { 'cierre real' }
$filterText = @()
if ($prefixes.Count -gt 0) { $filterText += "prefijo [$($prefixes -join ', ')]" }
if ($exacts.Count -gt 0) { $filterText += "exacto [$($exacts -join ', ')]" }
$treeText = ''
if ($Tree) {
    $treeText = ' con -Tree'
    if ($allows.Count -gt 0) { $treeText += " (helpers permitidos [$($allows -join ', ')])" }
}
Write-Line "$ExeName por $($filterText -join ' + ')$treeText  ($mode)"

# --- Filtros ---

function Get-NormPath([string] $path) {
    try { return [System.IO.Path]::GetFullPath($path) } catch { return $path }
}

function Test-InList([string] $norm, [string[]] $folders) {
    foreach ($f in $folders) {
        if ($norm.StartsWith($f, [System.StringComparison]::OrdinalIgnoreCase)) { return $true }
    }
    return $false
}

function Test-PathMatch([string] $path) {
    $norm = Get-NormPath $path
    foreach ($e in $exacts) {
        if ($norm.Equals($e, [System.StringComparison]::OrdinalIgnoreCase)) { return $true }
    }
    return (Test-InList $norm $prefixes)
}

# Un descendiente se cierra si esta en el filtro principal o en un -TreeAllow.
function Test-TreeMatch([string] $path) {
    if (Test-PathMatch $path) { return $true }
    return (Test-InList (Get-NormPath $path) $allows)
}

# --- Consulta ---

try {
    $procs = @(Get-CimInstance Win32_Process -Filter "Name='$ExeName'")
} catch {
    Write-Line "no se pudo consultar Win32_Process ($($_.Exception.Message)); no se cierra nada."
    exit 0
}

if ($procs.Count -eq 0) {
    Write-Line "no hay ningun $ExeName corriendo."
    exit 0
}

$allProcs = @()
if ($Tree) {
    try {
        $allProcs = @(Get-CimInstance Win32_Process)
    } catch {
        Write-Line "no se pudo listar los procesos para -Tree ($($_.Exception.Message)); no se cierra nada."
        exit 0
    }
}

# Descendientes de $root, en orden de recorrido. Cada uno: Proc, Depth, Action
# ('cerrar', 'fuera', 'sin-ruta', 'rama-viva'). Un hijo solo cuenta como tal si nacio despues que
# su padre: si el padre murio y su PID se reuso, el "hijo" es de otro.
function Get-Descendants($root) {
    $result = @()
    $queue = New-Object System.Collections.Queue
    $queue.Enqueue(@{ Proc = $root; Depth = 0; Alive = $false })
    $seen = @{ ([int]$root.ProcessId) = $true }
    while ($queue.Count -gt 0) {
        $node = $queue.Dequeue()
        $parent = $node.Proc
        foreach ($c in $allProcs) {
            $cid = [int]$c.ProcessId
            if ($c.ParentProcessId -ne $parent.ProcessId -or $seen.ContainsKey($cid)) { continue }
            if ($null -ne $parent.CreationDate -and $null -ne $c.CreationDate -and $c.CreationDate -lt $parent.CreationDate) { continue }
            $seen[$cid] = $true
            if ($node.Alive) {
                $action = 'rama-viva'
            } elseif (-not $c.ExecutablePath) {
                $action = 'sin-ruta'
            } elseif (Test-TreeMatch $c.ExecutablePath) {
                $action = 'cerrar'
            } else {
                $action = 'fuera'
            }
            $entry = @{ Proc = $c; Depth = $node.Depth + 1; Action = $action }
            $result += $entry
            $queue.Enqueue(@{ Proc = $c; Depth = $node.Depth + 1; Alive = ($action -ne 'cerrar') })
        }
    }
    return ,$result
}

# Cierra un proceso por PID, revalidando justo antes que siga siendo el mismo.
# Devuelve 'cerrado', 'no-cerrado' o 'ya-no-esta'.
function Close-ByPid($p, [string] $path) {
    $procId = [int]$p.ProcessId
    $again = $null
    try { $again = Get-CimInstance Win32_Process -Filter "ProcessId=$procId" } catch { }
    if ($null -eq $again -or -not $again.ExecutablePath -or
        -not ($again.ExecutablePath.Equals($path, [System.StringComparison]::OrdinalIgnoreCase)) -or
        $again.CreationDate -ne $p.CreationDate) {
        return 'ya-no-esta'
    }
    try { Stop-Process -Id $procId -Force -ErrorAction Stop } catch { Write-Line "  Stop-Process PID ${procId}: $($_.Exception.Message)" }
    $alive = $null
    for ($i = 0; $i -lt 20; $i++) {
        $alive = Get-Process -Id $procId -ErrorAction SilentlyContinue
        if ($null -eq $alive) { break }
        Start-Sleep -Milliseconds 100
    }
    if ($null -eq $alive) { return 'cerrado' }
    return 'no-cerrado'
}

$closed = 0
$failed = 0
$wouldClose = 0
$left = 0
foreach ($p in $procs) {
    $procId = [int]$p.ProcessId
    $path = $p.ExecutablePath
    if (-not $path) {
        Write-Line "PID $procId sin ExecutablePath legible (otra sesion o sin permisos): se deja vivo."
        $left++
        continue
    }
    if (-not (Test-PathMatch $path)) {
        Write-Line "PID $procId fuera del filtro, se deja vivo: $path"
        $left++
        continue
    }

    $desc = @()
    if ($Tree) {
        $live = @($allProcs | Where-Object { [int]$_.ProcessId -eq $procId })
        if ($live.Count -gt 0) { $desc = Get-Descendants $live[0] }
    }

    if ($DryRun) {
        Write-Line "PID $procId cerraria: $path"
        $wouldClose++
    } else {
        $r = Close-ByPid $p $path
        switch ($r) {
            'cerrado'    { Write-Line "PID $procId cerrado: $path"; $closed++ }
            'no-cerrado' { Write-Line "PID $procId NO SE PUDO CERRAR, sigue vivo: $path"; $failed++ }
            default      { Write-Line "PID $procId ya no es el mismo proceso (termino solo): no se toca." }
        }
    }

    # Descendientes: el padre se cierra primero para que no relance a sus hijos.
    foreach ($d in $desc) {
        $dp = $d.Proc
        $did = [int]$dp.ProcessId
        $indent = '  ' * $d.Depth
        $dpath = if ($dp.ExecutablePath) { $dp.ExecutablePath } else { '(ruta no legible)' }
        switch ($d.Action) {
            'fuera'     { Write-Line "${indent}descendiente PID $did fuera del prefijo y de -TreeAllow, se deja vivo: $dpath"; $left++; continue }
            'sin-ruta'  { Write-Line "${indent}descendiente PID $did sin ruta legible, se deja vivo"; $left++; continue }
            'rama-viva' { Write-Line "${indent}descendiente PID $did cuelga de uno que se deja vivo, no se toca: $dpath"; $left++; continue }
        }
        if ($d.Action -ne 'cerrar') { continue }
        if ($DryRun) {
            Write-Line "${indent}descendiente PID $did cerraria: $dpath"
            $wouldClose++
            continue
        }
        $r = Close-ByPid $dp $dp.ExecutablePath
        switch ($r) {
            'cerrado'    { Write-Line "${indent}descendiente PID $did cerrado: $dpath"; $closed++ }
            'no-cerrado' { Write-Line "${indent}descendiente PID $did NO SE PUDO CERRAR, sigue vivo: $dpath"; $failed++ }
            default      { Write-Line "${indent}descendiente PID $did ya no esta (termino con su padre)." }
        }
    }
}

if ($DryRun) {
    Write-Line "resumen (dry-run): cerraria $wouldClose, deja vivos $left."
} else {
    Write-Line "resumen: cerrados $closed, no se pudieron cerrar $failed, dejados vivos $left."
}
exit 0
