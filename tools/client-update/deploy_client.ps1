# Copy patched Wow.exe + MPQs into the Bonesaw client. Refuses if Wow is running.
$ErrorActionPreference = "Stop"
$Repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not (Test-Path (Join-Path $Repo "tools\client-patch\build_patch.py"))) {
    $Repo = "A:\wow-bonesaw"
}
$Client = "B:\Games\WoW 3.3.5\Bonesaw"
$PatchDist = Join-Path $Repo "tools\client-patch\dist"

if (Get-Process -Name "Wow","Wow-Bonesaw" -ErrorAction SilentlyContinue) {
    throw "Close Wow / Wow-Bonesaw first. The client has the MPQs and Wow.exe locked."
}

$patched = Join-Path $Client "Wow-Bonesaw.exe"
$wow = Join-Path $Client "Wow.exe"
$stock = Join-Path $Client "Wow.exe.stock"
if (Test-Path $patched) {
    if (-not (Test-Path $stock)) {
        Copy-Item $wow $stock
        Write-Host "Saved stock client as Wow.exe.stock"
    }
    Copy-Item -Force $patched $wow
    Write-Host "Wow.exe is now the patched Bonesaw client"
}

$y = Join-Path $PatchDist "patch-Y.MPQ"
if (Test-Path $y) {
    Copy-Item -Force $y (Join-Path $Client "Data\patch-Y.MPQ")
    Write-Host "Deployed Data\patch-Y.MPQ"
}
foreach ($locale in @("enUS", "enGB")) {
    $src = Join-Path $PatchDist "patch-$locale-4.MPQ"
    if (-not (Test-Path $src)) {
        continue
    }
    $dir = Join-Path $Client "Data\$locale"
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    Copy-Item -Force $src (Join-Path $dir "patch-$locale-4.MPQ")
    Write-Host "Deployed Data\$locale\patch-$locale-4.MPQ"
}

Copy-Item -Force (Join-Path $PSScriptRoot "Bonesaw.bat") $Client
Copy-Item -Force (Join-Path $PSScriptRoot "BonesawLauncher.ps1") $Client
Copy-Item -Force (Join-Path $PSScriptRoot "Bonesaw.version") $Client
Copy-Item -Force (Join-Path $PSScriptRoot "Bonesaw.update.json") $Client

$addonSrc = Join-Path $Repo "modules\mod-living-gear\client_addon\LivingGear"
$addonDst = Join-Path $Client "Interface\AddOns\LivingGear"
if (Test-Path $addonSrc) {
    New-Item -ItemType Directory -Force -Path $addonDst | Out-Null
    Copy-Item -Force (Join-Path $addonSrc "LivingGear.lua") $addonDst
    Copy-Item -Force (Join-Path $addonSrc "LivingGear.toc") $addonDst
    Write-Host "Deployed Interface\AddOns\LivingGear"
}

Write-Host "Done. Launch with Bonesaw.bat (update check) or Wow.exe."
