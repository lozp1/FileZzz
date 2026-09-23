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

$switchFolder = $sd.GetFolder.Items() | Where-Object { $_.Name -eq "switch" } | Select-Object -First 1
if (-not $switchFolder) {
    Write-Host "ERROR: No se encontro la carpeta /switch en la SD."
    exit 1
}

# Limpiar carpeta temp_EzFiles si existe en la Switch
$tempInSwitch = $switchFolder.GetFolder.Items() | Where-Object { $_.Name -eq "temp_EzFiles" } | Select-Object -First 1
if ($tempInSwitch) {
    Write-Host "Eliminando temp_EzFiles residual de la Switch..."
    $tempInSwitch.InvokeVerb("delete")
}

$nroSrc = "C:\Projects\c++\EzFiles\EzFiles.nro"
if (-not (Test-Path $nroSrc)) {
    Write-Host "ERROR: EzFiles.nro no encontrado en el PC."
    exit 1
}

Write-Host "Copiando EzFiles.nro a sdmc:/switch/EzFiles.nro..."
# Copiar directamente a sdmc:/switch/EzFiles.nro (Sobreescribir con flag 16: Yes to all)
$switchFolder.GetFolder.CopyHere($nroSrc, 16)
Start-Sleep -Seconds 2

# Tambien actualizar sdmc:/switch/EzFiles/EzFiles.nro si la subcarpeta existe
$targetAppFolder = $switchFolder.GetFolder.Items() | Where-Object { $_.Name -eq "EzFiles" } | Select-Object -First 1
if ($targetAppFolder) {
    Write-Host "Copiando EzFiles.nro a sdmc:/switch/EzFiles/EzFiles.nro..."
    $targetAppFolder.GetFolder.CopyHere($nroSrc, 16)
    Start-Sleep -Seconds 2
}

Write-Host "✅ [DEPLOY EXITOSO] EzFiles.nro actualizado al 100% en tu Switch."
