# Warn online players, wait, then saveall. After this script exits 0, docker compose
# replace/restart of ac-worldserver is allowed.
# Does not reboot the container. Do not use AzerothCore "server shutdown" here: that
# stops the process on its own timer and races saveall plus docker compose up.
# SOAP passwords never printed. Do not pipe to docker attach.

[CmdletBinding()]
param(
    [string]$Container = "ac-worldserver",
    [int]$DelaySeconds = 300,
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

# 2026-08-22: the warning was a single announce 45 seconds out. Raised to five
# minutes on the grounds that 45 seconds is never enough to actually act on --
# not enough to finish a boss, leave a dungeon, or land a flight path, so in
# practice it read as "you are about to be kicked" rather than as a warning.
#
# The countdown is staged rather than a single message. One notice five minutes
# ahead is easy to miss if you happen to be in a loading screen or looking away;
# repeating it as the window closes means anyone still online has had several
# chances to see it.
function Format-Countdown {
    param([int]$Seconds)
    if ($Seconds -ge 120) { return "$([math]::Round($Seconds / 60)) minutes" }
    if ($Seconds -ge 60)  { return "1 minute" }
    return "$Seconds seconds"
}

# Points before the restart at which to speak up, longest first.
$warnAt = @(300, 120, 60, 30, 10)

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

function Send-Warning {
    param([string]$Text)
    Write-Host "Announcing: $Text"
    $announceExit = Invoke-ConsoleLine -Line "announce $Text"
    if ($announceExit -ne 0) {
        return $false
    }
    $notifyExit = Invoke-ConsoleLine -Line "notify $Text"
    if ($notifyExit -ne 0) {
        Write-Host "notify failed; chat announce already sent"
    }
    return $true
}

# An explicit -Message overrides the staged countdown entirely -- callers that
# pass one want to say a specific thing, not be second-guessed.
$firstText = if ($Message) { $Message } else {
    "Server restart in $(Format-Countdown -Seconds $DelaySeconds). Your character will be saved. Finish up and find somewhere safe."
}
if (-not (Send-Warning -Text $firstText)) {
    Write-RestartResult -Ok $false -Text "could not announce restart warning"
}

if ($AnnounceOnly) {
    Write-RestartResult -Ok $true -Text "announce sent (AnnounceOnly); save/reboot not armed"
}

Write-Host "Waiting $DelaySeconds seconds before saveall..."
if ($DelaySeconds -gt 0) {
    # Sleep between checkpoints rather than in one block, announcing at each.
    # $remaining tracks real time left so the messages stay truthful even if a
    # console call takes a moment.
    $remaining = $DelaySeconds
    foreach ($mark in $warnAt) {
        if ($mark -ge $remaining) { continue }
        Start-Sleep -Seconds ($remaining - $mark)
        $remaining = $mark
        if (-not $Message) {
            [void](Send-Warning -Text "Server restart in $(Format-Countdown -Seconds $mark).")
        }
    }
    if ($remaining -gt 0) {
        Start-Sleep -Seconds $remaining
    }
}

$saveExit = Invoke-SaveAll
if ($saveExit -ne 0) {
    Write-RestartResult -Ok $false -Text "saveall failed; do not reboot"
}

Write-RestartResult -Ok $true -Text "players warned (${DelaySeconds}s, staged) and saved; restart is allowed"
