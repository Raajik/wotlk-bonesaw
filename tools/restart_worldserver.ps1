# Warn online players, wait, then saveall. After this script exits 0, docker compose
# replace/restart of ac-worldserver is allowed.
# Does not reboot the container. Do not use AzerothCore "server shutdown" here: that
# stops the process on its own timer and races saveall plus docker compose up.
# SOAP passwords never printed. Do not pipe to docker attach.

[CmdletBinding()]
param(
    [string]$Container = "ac-worldserver",
    [int]$DelaySeconds = 45,
    [string]$RepoRoot = "",
    [string]$Message = "",
    [switch]$AnnounceOnly
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

if (-not $RepoRoot) {
    $RepoRoot = Split-Path -Parent $PSScriptRoot
}

$saveScript = Join-Path $PSScriptRoot "save_world.ps1"
if (-not $Message) {
    $Message = "Worldserver restart in $DelaySeconds seconds. Your character will be saved."
}

function Write-RestartResult {
    param(
        [Parameter(Mandatory = $true)][bool]$Ok,
        [Parameter(Mandatory = $true)][string]$Text
    )
    if ($Ok) {
        Write-Host "OK: $Text"
        exit 0
    }
    Write-Host "FAIL: $Text"
    exit 1
}

function Test-ContainerRunning {
    param([string]$Name)
    $inspect = & docker inspect -f "{{.State.Running}}" $Name 2>$null
    if ($LASTEXITCODE -ne 0) {
        return $false
    }
    return ($inspect.Trim() -eq "true")
}

function Invoke-ConsoleLine {
    param([string]$Line)
    $py = Get-Command python -ErrorAction SilentlyContinue
    if (-not $py) {
        Write-Host "console attach needs python on PATH"
        return 1
    }
    $helper = Join-Path $RepoRoot "tools\worldserver_cli.py"
    $out = & python $helper --container $Container --command $Line --wait 2 2>&1
    $exit = $LASTEXITCODE
    if ($null -ne $out -and "$out".Trim()) {
        Write-Host ("$out".Trim())
    }
    return [int]$exit
}

function Invoke-SaveAll {
    $pwsh = Get-Command powershell -ErrorAction SilentlyContinue
    if (-not $pwsh) {
        return 1
    }
    $out = & powershell -NoProfile -File $saveScript -Container $Container -RepoRoot $RepoRoot -WaitSeconds 3 2>&1
    $exit = $LASTEXITCODE
    if ($null -ne $out -and "$out".Trim()) {
        Write-Host ("$out".Trim())
    }
    return [int]$exit
}

if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
    Write-RestartResult -Ok $false -Text "docker is not available"
}

if (-not (Test-ContainerRunning -Name $Container)) {
    Write-RestartResult -Ok $true -Text "$Container is not running; warn/save skipped; restart is allowed"
}

Write-Host "Announcing: $Message"
$announceExit = Invoke-ConsoleLine -Line "announce $Message"
if ($announceExit -ne 0) {
    Write-RestartResult -Ok $false -Text "could not announce restart warning"
}

$notifyExit = Invoke-ConsoleLine -Line "notify $Message"
if ($notifyExit -ne 0) {
    Write-Host "notify failed; chat announce already sent"
}

if ($AnnounceOnly) {
    Write-RestartResult -Ok $true -Text "announce sent (AnnounceOnly); save/reboot not armed"
}

Write-Host "Waiting $DelaySeconds seconds before saveall..."
if ($DelaySeconds -gt 0) {
    Start-Sleep -Seconds $DelaySeconds
}

$saveExit = Invoke-SaveAll
if ($saveExit -ne 0) {
    Write-RestartResult -Ok $false -Text "saveall failed; do not reboot"
}

Write-RestartResult -Ok $true -Text "players warned (${DelaySeconds}s) and saved; restart is allowed"
