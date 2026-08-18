# Bonesaw client launcher. Checks GitHub releases, then starts Wow.exe.
# Safe rules: allowlisted paths only, SHA256 required, refuse if Wow is running.

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
if (Test-Path (Join-Path $Root "Wow.exe")) {
    $Client = $Root
} else {
    $Client = "B:\Games\WoW 3.3.5\Bonesaw"
}

$Allow = @(
    "Wow.exe",
    "Data\patch-Y.MPQ",
    "Data\enUS\patch-enUS-4.MPQ",
    "Data\enGB\patch-enGB-4.MPQ",
    "Bonesaw.version"
)

function Get-LocalVersion {
    $p = Join-Path $Client "Bonesaw.version"
    if (Test-Path $p) { return (Get-Content $p -Raw).Trim() }
    return "0.0.0"
}

function Test-WowRunning {
    return [bool](Get-Process -Name "Wow","Wow-Bonesaw" -ErrorAction SilentlyContinue)
}

function Get-Sha256([string]$Path) {
    return (Get-FileHash -Algorithm SHA256 -Path $Path).Hash.ToLowerInvariant()
}

function Compare-Version([string]$A, [string]$B) {
    $pa = @($A.Split(".") | ForEach-Object { [int]($_ -replace "\D","0") })
    $pb = @($B.Split(".") | ForEach-Object { [int]($_ -replace "\D","0") })
    while ($pa.Count -lt 3) { $pa += 0 }
    while ($pb.Count -lt 3) { $pb += 0 }
    for ($i = 0; $i -lt 3; $i++) {
        if ($pa[$i] -lt $pb[$i]) { return -1 }
        if ($pa[$i] -gt $pb[$i]) { return 1 }
    }
    return 0
}

function Test-AllowedPath([string]$RelPath) {
    if ($RelPath -match '\.\.' -or $RelPath.StartsWith("\") -or $RelPath.Contains(":")) {
        return $false
    }
    return $Allow -contains $RelPath
}

function Update-FromGitHub {
    $cfgPath = Join-Path $Client "Bonesaw.update.json"
    if (-not (Test-Path $cfgPath)) { return }
    $cfg = Get-Content $cfgPath -Raw | ConvertFrom-Json
    if (-not $cfg.github) {
        Write-Host "Bonesaw updater: no github repo set. Launching local client."
        return
    }
    if ($cfg.github -notmatch '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$') {
        throw "Bonesaw.update.json github must be owner/repo"
    }

    $local = Get-LocalVersion
    $api = "https://api.github.com/repos/$($cfg.github)/releases/latest"
    Write-Host "Bonesaw updater: local $local  checking $api"
    try {
        $rel = Invoke-RestMethod -Uri $api -Headers @{ "User-Agent" = "BonesawLauncher" }
    } catch {
        Write-Host "Bonesaw updater: GitHub check failed. Launching local client."
        return
    }

    $remoteVer = ($rel.tag_name -replace "^v","").Trim()
    if ((Compare-Version $local $remoteVer) -ge 0) {
        Write-Host "Bonesaw updater: up to date ($local)."
        return
    }

    $manifestAsset = $rel.assets | Where-Object { $_.name -eq "Bonesaw.manifest.json" } | Select-Object -First 1
    if (-not $manifestAsset) {
        Write-Host "Bonesaw updater: release $remoteVer has no Bonesaw.manifest.json. Skipping."
        return
    }

    if (Test-WowRunning) {
        Write-Host "Bonesaw updater: Wow is running. Close it to apply $remoteVer."
        return
    }

    $stage = Join-Path $Client "update-staging"
    if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
    New-Item -ItemType Directory -Force -Path $stage | Out-Null
    $manFile = Join-Path $stage "Bonesaw.manifest.json"
    Invoke-WebRequest -Uri $manifestAsset.browser_download_url -OutFile $manFile -UseBasicParsing
    $man = Get-Content $manFile -Raw | ConvertFrom-Json

    if (-not $man.version -or -not $man.files) {
        throw "Manifest is missing version or files"
    }
    if ((Compare-Version $man.version.Trim() $remoteVer) -ne 0) {
        throw "Manifest version $($man.version) does not match release tag $remoteVer"
    }

    foreach ($f in $man.files) {
        if (-not $f.path -or -not $f.asset -or -not $f.sha256) {
            throw "Manifest file entry is missing path, asset, or sha256"
        }
        if ($f.asset -match '[\\/]' -or $f.asset -match '\.\.') {
            throw "Refusing asset name with a path: $($f.asset)"
        }
        $relPath = ($f.path -replace "/","\")
        if (-not (Test-AllowedPath $relPath)) {
            throw "Refusing to update non-allowlisted path: $relPath"
        }
        $asset = $rel.assets | Where-Object { $_.name -eq $f.asset } | Select-Object -First 1
        if (-not $asset) { throw "Missing release asset $($f.asset)" }
        $dest = Join-Path $stage $relPath
        New-Item -ItemType Directory -Force -Path (Split-Path $dest) | Out-Null
        Write-Host "Downloading $($f.asset) ..."
        Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $dest -UseBasicParsing
        $hash = Get-Sha256 $dest
        if ($hash -ne $f.sha256.ToLowerInvariant()) {
            throw "Hash mismatch for $($f.asset): $hash"
        }
    }

    foreach ($f in $man.files) {
        $relPath = ($f.path -replace "/","\")
        $src = Join-Path $stage $relPath
        $dst = Join-Path $Client $relPath
        New-Item -ItemType Directory -Force -Path (Split-Path $dst) | Out-Null
        Copy-Item -Force $src $dst
        Write-Host "Updated $relPath"
    }

    Set-Content -Path (Join-Path $Client "Bonesaw.version") -Value $man.version.Trim() -NoNewline
    Remove-Item -Recurse -Force $stage
    Write-Host "Bonesaw updater: now at $($man.version)."
}

function Start-Wow {
    $wow = Join-Path $Client "Wow.exe"
    if (-not (Test-Path $wow)) { throw "Missing $wow" }
    Start-Process -FilePath $wow -WorkingDirectory $Client
}

Update-FromGitHub
Start-Wow
