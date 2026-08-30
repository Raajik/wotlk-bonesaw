# Saves a Bonesaw auto-login, encrypted with DPAPI to the current Windows
# user. The launcher decrypts it on launch and types it into the Wow login
# screen. The blob can only be decrypted by THIS Windows user on THIS
# machine -- it is convenience-grade, not vault-grade: anything running as
# the same Windows user could decrypt it too. Delete Bonesaw.login in the
# client folder to go back to manual login.
#
# Run from the repo:  powershell -File tools\client-update\save_login.ps1
# (optional first arg = client folder, default matches deploy_client.ps1)

$ErrorActionPreference = "Stop"
$Client = if ($args.Count -ge 1) { $args[0] } else { "B:\Games\WoW 3.3.5\Bonesaw" }
if (-not (Test-Path (Join-Path $Client "Wow.exe")) -and -not (Test-Path (Join-Path $Client "Wow.exe.stock"))) {
    throw "$Client does not look like the Bonesaw client folder. Pass the folder: save_login.ps1 <path>"
}

Add-Type -AssemblyName System.Security

$account = (Read-Host "Account name").Trim()
$secure = Read-Host "Password" -AsSecureString
$bstr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure)
try {
    $password = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($bstr)
} finally {
    [Runtime.InteropServices.Marshal]::ZeroBSTR($bstr)
}
if (-not $account -or -not $password) {
    throw "Both an account and a password are required."
}

$bytes = [Text.Encoding]::UTF8.GetBytes("$account`n$password")
$blob = [Security.Cryptography.ProtectedData]::Protect(
    $bytes, $null, [Security.Cryptography.DataProtectionScope]::CurrentUser)
[System.IO.File]::WriteAllBytes((Join-Path $Client "Bonesaw.login"), $blob)

Write-Host "Saved $Client\Bonesaw.login (DPAPI, this Windows user only)."
Write-Host "The launcher will type it in on launch. Delete the file to disable."
