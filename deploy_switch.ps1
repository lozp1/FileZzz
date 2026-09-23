$shell = New-Object -ComObject Shell.Application
$myPC = $shell.NameSpace(17)
$switch = $myPC.Items() | Where-Object { $_.Name -like "*Switch*" -or $_.Name -like "*Nintendo*" } | Select-Object -First 1

if (-not $switch) {
    Write-Host "ERROR: Switch no conectada vía DBI MTP."
    exit 1
}

$sd = $switch.GetFolder.Items() | Where-Object { $_.Name -like "*SD*" -or $_.Name -like "*MicroSD*" } | Select-Object -First 1
$switchFolder = $sd.GetFolder.Items() | Where-Object { $_.Name -eq "switch" } | Select-Object -First 1

$nroSrc = "C:\Projects\c++\EzFiles\EzFiles.nro"
if (-not (Test-Path $nroSrc)) {
    Write-Host "ERROR: EzFiles.nro no encontrado. Compila con 'make' primero."
    exit 1
}

# Carpeta de destino SD:/switch/EzFiles/
$targetAppFolder = $switchFolder.GetFolder.Items() | Where-Object { $_.Name -eq "EzFiles" } | Select-Object -First 1
if (-not $targetAppFolder) {
    $tempDir = "C:\Projects\c++\EzFiles\temp_EzFiles"
    if (Test-Path $tempDir) { Remove-Item $tempDir -Recurse -Force }
    New-Item -ItemType Directory -Path $tempDir | Out-Null
    Copy-Item $nroSrc (Join-Path $tempDir "EzFiles.nro") -Force
    $switchFolder.GetFolder.CopyHere($tempDir, 16)
    Start-Sleep -Seconds 2
    Remove-Item $tempDir -Recurse -Force
} else {
    $targetAppFolder.GetFolder.CopyHere($nroSrc, 16)
    Start-Sleep -Seconds 2
}

Write-Host "✅ [DEPLOY EXITOSO] EzFiles.nro copiado a SD:/switch/EzFiles/EzFiles.nro en tu Switch."
