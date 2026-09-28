$shell = New-Object -ComObject Shell.Application
$myPC = $shell.NameSpace(17)
$switch = $myPC.Items() | Where-Object { $_.Name -like "*Switch*" -or $_.Name -like "*Nintendo*" } | Select-Object -First 1

if (-not $switch) {
    Write-Host "ERROR: Switch no conectada via MTP."
    exit 1
}

$sd = $switch.GetFolder.Items() | Where-Object { $_.Name -like "*SD*" -or $_.Name -like "*MicroSD*" } | Select-Object -First 1
if (-not $sd) {
    Write-Host "ERROR: No se encontro la particion de la SD en la Switch."
    exit 1
}

$sw = $sd.GetFolder.Items() | Where-Object { $_.Name -ieq "switch" } | Select-Object -First 1
if (-not $sw) {
    Write-Host "ERROR: No se encontro la carpeta /switch en la SD."
    exit 1
}

$swFolder = $sw.GetFolder

# 1. Eliminar FileZzz.nro o archivos obsoletos directamente en /switch/
Write-Host "Revisando archivos en sdmc:/switch/..."
$itemsToDelete = $swFolder.Items() | Where-Object { 
    $_.Name -eq "FileZzz.nro" -or 
    $_.Name -like "EzFile*.nro" -or 
    $_.Name -eq "EzFiles.nro" 
}

foreach ($item in $itemsToDelete) {
    Write-Host "Eliminando de sdmc:/switch/: $($item.Name)"
    try {
        $item.InvokeVerb("delete")
        Start-Sleep -Milliseconds 500
    } catch {
        Write-Host "No se pudo eliminar $($item.Name): $_"
    }
}

# 2. Localizar o crear la carpeta sdmc:/switch/FileZzz/
$filezzzFolderItem = $swFolder.Items() | Where-Object { $_.Name -ieq "FileZzz" } | Select-Object -First 1
if (-not $filezzzFolderItem) {
    Write-Host "Creando carpeta sdmc:/switch/FileZzz..."
    $swFolder.NewFolder("FileZzz")
    Start-Sleep -Seconds 1
    $filezzzFolderItem = $swFolder.Items() | Where-Object { $_.Name -ieq "FileZzz" } | Select-Object -First 1
}

if (-not $filezzzFolderItem) {
    Write-Host "ERROR: No se pudo acceder a sdmc:/switch/FileZzz."
    exit 1
}

$appFolder = $filezzzFolderItem.GetFolder
$nroSrc = "C:\Projects\c++\EzFiles\FileZzz.nro"
$srcSize = (Get-Item $nroSrc).Length
Write-Host "Tamano local de FileZzz.nro: $srcSize bytes"

# Si ya existe en /switch/FileZzz/FileZzz.nro, eliminarlo antes para copia limpia
$existingAppNro = $appFolder.Items() | Where-Object { $_.Name -eq "FileZzz.nro" } | Select-Object -First 1
if ($existingAppNro) {
    Write-Host "Sobrescribiendo FileZzz.nro en sdmc:/switch/FileZzz/..."
    try {
        $existingAppNro.InvokeVerb("delete")
        Start-Sleep -Seconds 1
    } catch {}
}

Write-Host "Copiando FileZzz.nro exclusivamente a sdmc:/switch/FileZzz/FileZzz.nro..."
$appFolder.CopyHere($nroSrc, 16)

# Esperar a que la transferencia finalice
$success = $false
for ($i = 0; $i -lt 30; $i++) {
    Start-Sleep -Seconds 1
    $destItem = $appFolder.Items() | Where-Object { $_.Name -eq "FileZzz.nro" } | Select-Object -First 1
    if ($destItem -and $destItem.Size -gt 0) {
        Write-Host "Progreso: $($destItem.Size) / $srcSize bytes..."
        if ($destItem.Size -eq $srcSize) {
            Write-Host "Transferencia a sdmc:/switch/FileZzz/FileZzz.nro completada con exito!"
            $success = $true
            break
        }
    }
}

# 3. Verificacion final de que SOLO existe una entrada
Start-Sleep -Seconds 2
$rootCount = ($swFolder.Items() | Where-Object { $_.Name -eq "FileZzz.nro" }).Count
$subCount = ($appFolder.Items() | Where-Object { $_.Name -eq "FileZzz.nro" }).Count

Write-Host "=== VERIFICACION FINAL ==="
Write-Host "Archivos FileZzz.nro en /switch/: $rootCount (debe ser 0)"
Write-Host "Archivos FileZzz.nro en /switch/FileZzz/: $subCount (debe ser 1)"

if ($rootCount -eq 0 -and $subCount -eq 1) {
    Write-Host "RESULTADO: PERFECTO. Solo existe 1 entrada en el menu homebrew."
} else {
    Write-Host "ADVERTENCIA: Revisar cantidades de archivos."
}
