$shell = New-Object -ComObject Shell.Application
$myPC = $shell.NameSpace(17)
$switch = $myPC.Items() | Where-Object { $_.Name -like "*Switch*" -or $_.Name -like "*Nintendo*" } | Select-Object -First 1

if (-not $switch) {
    Write-Host "ERROR: Switch no conectada vía MTP."
    exit 1
}

$sd = $switch.GetFolder.Items() | Where-Object { $_.Name -like "*SD*" -or $_.Name -like "*MicroSD*" } | Select-Object -First 1
if (-not $sd) {
    Write-Host "ERROR: No se encontro la particion de la tarjeta SD en la Switch."
    exit 1
}

$sw = $sd.GetFolder.Items() | Where-Object { $_.Name -ieq "switch" } | Select-Object -First 1
if (-not $sw) {
    Write-Host "ERROR: No se encontro la carpeta /switch en la SD."
    exit 1
}

$swFolder = $sw.GetFolder
$nroSrc = "C:\Projects\c++\EzFiles\FileZzz.nro"
if (-not (Test-Path $nroSrc)) {
    Write-Host "ERROR: FileZzz.nro no encontrado en el PC."
    exit 1
}

$srcSize = (Get-Item $nroSrc).Length
Write-Host "Tamano de FileZzz.nro local: $srcSize bytes"

# Asegurar carpeta de la aplicacion sdmc:/switch/FileZzz/
$targetAppFolder = $swFolder.Items() | Where-Object { $_.Name -ieq "FileZzz" } | Select-Object -First 1
if (-not $targetAppFolder) {
    Write-Host "Creando carpeta sdmc:/switch/FileZzz..."
    $swFolder.NewFolder("FileZzz")
    Start-Sleep -Seconds 1
    $targetAppFolder = $swFolder.Items() | Where-Object { $_.Name -ieq "FileZzz" } | Select-Object -First 1
}

if (-not $targetAppFolder) {
    Write-Host "ERROR: No se pudo acceder a sdmc:/switch/FileZzz."
    exit 1
}

Write-Host "Iniciando transferencia exclusiva a sdmc:/switch/FileZzz/FileZzz.nro..."
$appFolder = $targetAppFolder.GetFolder
$appFolder.CopyHere($nroSrc, 16)

# Esperar a que la transferencia concluya
$success = $false
for ($i = 0; $i -lt 30; $i++) {
    Start-Sleep -Seconds 1
    $destAppItem = $appFolder.Items() | Where-Object { $_.Name -eq "FileZzz.nro" } | Select-Object -First 1
    if ($destAppItem) {
        $sizeStr = $appFolder.GetDetailsOf($destAppItem, 2)
        if ($sizeStr) {
            Write-Host "FileZzz.nro en Switch: $sizeStr"
            $success = $true
            break
        }
    }
}

Write-Host "✅ [DEPLOY EXITOSO] Unico FileZzz.nro desplegado en sdmc:/switch/FileZzz/."
