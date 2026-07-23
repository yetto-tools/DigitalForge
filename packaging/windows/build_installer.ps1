# Genera el instalador de Windows de DigitalForge (compila la GUI, corre
# windeployqt y empaqueta todo con Inno Setup). Requiere tener Inno Setup 6
# instalado (https://jrsoftware.org/isdl.php).

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$buildDir = Join-Path $repoRoot "build-gui"
$distDir = Join-Path $PSScriptRoot "dist\DigitalForge"
$outputDir = Join-Path $PSScriptRoot "output"

$qtBin = "C:\Qt\6.7.3\mingw_64\bin"
$mingwBin = "C:\Qt\Tools\mingw1310_64\bin"
if (-not (Test-Path $qtBin)) { throw "No se encontro Qt en $qtBin" }
if (-not (Test-Path $mingwBin)) { throw "No se encontro MinGW en $mingwBin" }
$env:PATH = "$qtBin;$mingwBin;$env:PATH"

$cmakeListsPath = Join-Path $repoRoot "CMakeLists.txt"
$cmakeContent = Get-Content $cmakeListsPath -Raw
if ($cmakeContent -notmatch "VERSION\s+(\d+\.\d+\.\d+)") {
    throw "No se pudo leer la version del proyecto desde CMakeLists.txt"
}
$version = $Matches[1]
Write-Host "Version detectada: $version"

Write-Host "Configurando build-gui (Release, GUI habilitada)..."
cmake -S $repoRoot -B $buildDir -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DDIGITALFORGE_BUILD_GUI=ON
if ($LASTEXITCODE -ne 0) { throw "Fallo la configuracion de CMake" }

Write-Host "Compilando DigitalForge.exe..."
cmake --build $buildDir --target DigitalForge -j
if ($LASTEXITCODE -ne 0) { throw "Fallo la compilacion" }

$exePath = Join-Path $buildDir "src\app\DigitalForge.exe"
if (-not (Test-Path $exePath)) { throw "No se encontro el ejecutable compilado en $exePath" }

Write-Host "Preparando carpeta de staging..."
if (Test-Path $distDir) { Remove-Item $distDir -Recurse -Force }
New-Item -ItemType Directory -Path $distDir -Force | Out-Null
Copy-Item $exePath $distDir

Write-Host "Ejecutando windeployqt (Qt + runtime de MinGW)..."
$windeployqt = Join-Path $qtBin "windeployqt.exe"
& $windeployqt --release --compiler-runtime (Join-Path $distDir "DigitalForge.exe")
if ($LASTEXITCODE -ne 0) { throw "Fallo windeployqt" }

# windeployqt (via --compiler-runtime) puede terminar copiando el runtime que
# trae la propia instalacion de Qt (mas viejo) en lugar del que coincide con
# el compilador mingw1310_64 usado para compilar DigitalForge.exe, lo cual
# produce errores "Entry Point Not Found" (p.ej. simbolos de libstdc++ que
# solo existen en el GCC mas nuevo). Se sobrescriben explicitamente con el
# runtime del toolchain correcto para garantizar que coincidan.
Write-Host "Asegurando el runtime de MinGW correcto (mingw1310_64)..."
foreach ($dll in @("libstdc++-6.dll", "libgcc_s_seh-1.dll", "libwinpthread-1.dll")) {
    Copy-Item (Join-Path $mingwBin $dll) $distDir -Force
}

# Componentes definidos en JSON (Fase 3): se cargan desde "<carpeta del exe>\components"
# al arrancar, asi que deben viajar junto al ejecutable dentro del instalador.
$componentsSrc = Join-Path $repoRoot "components"
if (Test-Path $componentsSrc) {
    Write-Host "Incluyendo la biblioteca de componentes JSON..."
    Copy-Item $componentsSrc (Join-Path $distDir "components") -Recurse -Force
}

$isccCandidates = @(
    "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
    "C:\Program Files\Inno Setup 6\ISCC.exe",
    (Join-Path $env:LOCALAPPDATA "Programs\Inno Setup 6\ISCC.exe")
)
$iscc = $isccCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) {
    $isccCmd = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($isccCmd) { $iscc = $isccCmd.Source }
}
if (-not $iscc) {
    throw "No se encontro ISCC.exe (Inno Setup Compiler). Instalalo desde https://jrsoftware.org/isdl.php y volve a correr este script."
}

if (Test-Path $outputDir) { Remove-Item $outputDir -Recurse -Force }
New-Item -ItemType Directory -Path $outputDir -Force | Out-Null

Write-Host "Generando instalador con Inno Setup..."
$issPath = Join-Path $PSScriptRoot "DigitalForge.iss"
& $iscc "/DMyAppVersion=$version" $issPath
if ($LASTEXITCODE -ne 0) { throw "Fallo la compilacion del instalador" }

Write-Host ""
Write-Host "Instalador generado en: $outputDir"
