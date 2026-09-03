# Patch stock 3.3.5a 12340 Wow.exe so Bonesaw FrameXML MPQs can load.
# Same edits as tools/client-patch/patch_wow_exe.py. Does not download an exe.

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not (Test-Path (Join-Path $Root "Wow.exe"))) {
    $Root = (Get-Location).Path
}
$Wow = Join-Path $Root "Wow.exe"
$Stock = Join-Path $Root "Wow.exe.stock"
$ExpectedSize = 7704216

if (-not (Test-Path $Wow)) {
    throw "Put BonesawPatchExe.ps1 in the folder that has Wow.exe, then run BonesawPatchExe.bat."
}
if (Get-Process -Name "Wow","Wow-Bonesaw" -ErrorAction SilentlyContinue) {
    throw "Close Wow first."
}

$Patches = @(
    @{ Off = 0x1F41BF; Orig = [byte[]](0x74); Repl = [byte[]](0xEB) },
    @{ Off = 0x415A25; Orig = [byte[]](0x75); Repl = [byte[]](0xEB) },
    @{ Off = 0x415A3F; Orig = [byte[]](0x01); Repl = [byte[]](0x03) },
    @{ Off = 0x415A95; Orig = [byte[]](0x01); Repl = [byte[]](0x03) },
    @{ Off = 0x415B46; Orig = [byte[]](0x7F); Repl = [byte[]](0xEB) },
    @{ Off = 0x415B5F; Orig = [byte[]](0x83, 0xC0, 0x03, 0x5E, 0x8B, 0xE5, 0x5D); Repl = [byte[]](0xB8, 0x03, 0x00, 0x00, 0x00, 0xEB, 0xED) },
    @{ Off = 0x123676; Orig = [byte[]](0x8B, 0xCE, 0xE8, 0xA3, 0x18, 0x1F, 0x00); Repl = [byte[]](0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90) },
    @{ Off = 0x1241C6; Orig = [byte[]](0x50); Repl = [byte[]](0x90) },
    @{ Off = 0x1241C9; Orig = [byte[]](0xE8, 0x82, 0xC0, 0x1F, 0x00); Repl = [byte[]](0x90, 0x90, 0x90, 0x90, 0x90) },
    @{ Off = 0x320273; Orig = [byte[]](0x0F, 0x85, 0xFE, 0x00, 0x00, 0x00); Repl = [byte[]](0x90, 0x90, 0x90, 0x90, 0x90, 0x90) },
    @{ Off = 0x320282; Orig = [byte[]](0x0F, 0x85, 0xEF, 0x00, 0x00, 0x00); Repl = [byte[]](0x90, 0x90, 0x90, 0x90, 0x90, 0x90) }
)

function Test-Bytes([byte[]]$Data, [int]$Off, [byte[]]$Want) {
    for ($i = 0; $i -lt $Want.Length; $i++) {
        if ($Data[$Off + $i] -ne $Want[$i]) {
            return $false
        }
    }
    return $true
}

$srcPath = $Wow
if (Test-Path $Stock) {
    $srcPath = $Stock
    Write-Host "Patching from Wow.exe.stock"
} elseif ((Get-Item $Wow).Length -eq $ExpectedSize) {
    Copy-Item -Force $Wow $Stock
    Write-Host "Saved stock client as Wow.exe.stock"
    $srcPath = $Stock
}

$data = [System.IO.File]::ReadAllBytes($srcPath)
if ($data.Length -ne $ExpectedSize) {
    throw "Wow.exe is $($data.Length) bytes. Need stock 3.3.5a build 12340 ($ExpectedSize bytes)."
}

$changed = 0
foreach ($p in $Patches) {
    if (Test-Bytes $data $p.Off $p.Repl) {
        continue
    }
    if (-not (Test-Bytes $data $p.Off $p.Orig)) {
        $got = ($data[$p.Off..($p.Off + $p.Orig.Length - 1)] | ForEach-Object { $_.ToString("x2") }) -join ""
        throw ("Mismatch at 0x{0:X}: {1}. This is not stock 12340, or it is already patched differently." -f $p.Off, $got)
    }
    [Array]::Copy($p.Repl, 0, $data, $p.Off, $p.Repl.Length)
    $changed++
}

[System.IO.File]::WriteAllBytes($Wow, $data)
if ($changed -eq 0) {
    Write-Host "Wow.exe was already patched. FrameXML MPQs can load."
} else {
    Write-Host "Patched Wow.exe ($changed edits). FrameXML MPQs can load."
}
Write-Host "Done. Run Bonesaw.bat or Wow.exe."
