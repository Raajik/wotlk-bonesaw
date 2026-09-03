# Save all online players on ac-worldserver before restart/replace.
# Prefer SOAP executeCommand when port 7878 is live and GM creds exist.
# Otherwise send through the Docker attach API (tools/worldserver_cli.py).
# Do not pipe to "docker attach": the CLI refuses non-TTY stdin, and EOF can halt the server.
# SOAP passwords never printed.
# Default: saveall. For a player warning then save, use tools/restart_worldserver.ps1.

[CmdletBinding()]
param(
    [string]$Container = "ac-worldserver",
    [int]$WaitSeconds = 3,
    [string]$RepoRoot = "",
    [string]$WorldCommand = "saveall",
    [string]$Expect = ""
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

if (-not $RepoRoot) {
    $RepoRoot = Split-Path -Parent $PSScriptRoot
}

if (-not $Expect -and $WorldCommand -eq "saveall") {
    $Expect = "All players saved"
}

function Write-SaveResult {
    param(
        [Parameter(Mandatory = $true)][bool]$Ok,
        [Parameter(Mandatory = $true)][string]$Message
    )
    if ($Ok) {
        Write-Host "OK: $Message"
        exit 0
    }
    Write-Host "FAIL: $Message"
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

function Get-ConfValue {
    param(
        [string]$Path,
        [string]$Key,
        [string]$Default = ""
    )
    if (-not (Test-Path $Path)) {
        return $Default
    }
    $escaped = [regex]::Escape($Key)
    $line = Select-String -Path $Path -Pattern "^\s*$escaped\s*=" | Select-Object -Last 1
    if (-not $line) {
        return $Default
    }
    $raw = ($line.Line -split "=", 2)[1].Trim()
    if ($raw.StartsWith('"') -and $raw.EndsWith('"') -and $raw.Length -ge 2) {
        $raw = $raw.Substring(1, $raw.Length - 2)
    }
    return $raw
}

function Test-TcpOpen {
    param(
        [string]$TargetHost,
        [int]$Port,
        [int]$TimeoutMs = 400
    )
    $client = New-Object System.Net.Sockets.TcpClient
    try {
        $iar = $client.BeginConnect($TargetHost, $Port, $null, $null)
        $ok = $iar.AsyncWaitHandle.WaitOne($TimeoutMs, $false)
        if (-not $ok) {
            return $false
        }
        $client.EndConnect($iar)
        return $client.Connected
    }
    catch {
        return $false
    }
    finally {
        $client.Close()
    }
}

function Get-SoapCredentials {
    param([string]$Root)
    $user = [string]$env:BONESAW_SOAP_USER
    $pass = [string]$env:BONESAW_SOAP_PASSWORD
    if (-not $user) { $user = [string]$env:SOAP_USER }
    if (-not $pass) { $pass = [string]$env:SOAP_PASSWORD }

    $files = @(
        (Join-Path $Root "tools\soap.env"),
        (Join-Path $Root "tools\client-update\soap.env")
    )
    foreach ($file in $files) {
        if (-not (Test-Path $file)) {
            continue
        }
        Get-Content $file | ForEach-Object {
            $line = $_.Trim()
            if (-not $line -or $line.StartsWith("#")) {
                return
            }
            $parts = $line -split "=", 2
            if ($parts.Count -ne 2) {
                return
            }
            $k = $parts[0].Trim()
            $v = $parts[1].Trim().Trim('"')
            if ($k -in @("SOAP_USER", "BONESAW_SOAP_USER") -and -not $user) { $user = $v }
            if ($k -in @("SOAP_PASSWORD", "BONESAW_SOAP_PASSWORD") -and -not $pass) { $pass = $v }
        }
    }

    if ($user -and $pass) {
        return @{ User = $user; Password = $pass }
    }
    return $null
}

function Escape-XmlText {
    param([string]$Text)
    return (($Text -replace "&", "&amp;") -replace "<", "&lt;") -replace ">", "&gt;"
}

function Invoke-SoapCommand {
    param(
        [string]$TargetHost,
        [int]$Port,
        [string]$User,
        [string]$Password,
        [string]$Command,
        [string]$ExpectText = ""
    )
    $pair = "{0}:{1}" -f $User, $Password
    $b64 = [Convert]::ToBase64String([Text.Encoding]::ASCII.GetBytes($pair))
    $safeCmd = Escape-XmlText -Text $Command
    $xml = @"
<?xml version="1.0" encoding="utf-8"?>
<SOAP-ENV:Envelope xmlns:SOAP-ENV="http://schemas.xmlsoap.org/soap/envelope/" xmlns:ns1="urn:AC">
  <SOAP-ENV:Body>
    <ns1:executeCommand>
      <command>$safeCmd</command>
    </ns1:executeCommand>
  </SOAP-ENV:Body>
</SOAP-ENV:Envelope>
"@
    $headers = @{ Authorization = "Basic $b64" }
    $uri = "http://${TargetHost}:${Port}/"
    try {
        $resp = Invoke-WebRequest -Uri $uri -Method POST -Headers $headers `
            -ContentType "text/xml; charset=utf-8" -Body $xml -UseBasicParsing -TimeoutSec 20
        $body = [string]$resp.Content
        if ($resp.StatusCode -lt 200 -or $resp.StatusCode -ge 300) {
            return @{ Ok = $false; Detail = "SOAP HTTP $($resp.StatusCode)" }
        }
        if ($ExpectText -and ($body -notmatch [regex]::Escape($ExpectText))) {
            return @{ Ok = $true; Detail = "SOAP $Command (HTTP $($resp.StatusCode), expected text not in body)" }
        }
        return @{ Ok = $true; Detail = "SOAP $Command" }
    }
    catch [System.Net.WebException] {
        $code = 0
        if ($_.Exception.Response) {
            $code = [int]$_.Exception.Response.StatusCode
        }
        if ($code -eq 401 -or $code -eq 403) {
            return @{ Ok = $false; Detail = "SOAP auth failed (HTTP $code)" }
        }
        if ($code -gt 0) {
            return @{ Ok = $false; Detail = "SOAP HTTP $code" }
        }
        return @{ Ok = $false; Detail = "SOAP not reachable" }
    }
    catch {
        return @{ Ok = $false; Detail = "SOAP not reachable" }
    }
}

function Invoke-StdinCommand {
    param(
        [string]$Name,
        [string]$Root,
        [string]$Command,
        [string]$ExpectText = ""
    )
    $py = Get-Command python -ErrorAction SilentlyContinue
    if (-not $py) {
        Write-Host "console attach needs python on PATH"
        return $false
    }
    $helper = Join-Path $Root "tools\worldserver_cli.py"
    $pyWait = 5
    if (-not $ExpectText) {
        $pyWait = 2
    }
    $pyArgs = @(
        $helper,
        "--container", $Name,
        "--command", $Command,
        "--wait", "$pyWait"
    )
    if ($ExpectText) {
        $pyArgs += @("--expect", $ExpectText)
    }
    & python @pyArgs
    if ($LASTEXITCODE -ne 0) {
        return $false
    }
    return $true
}

if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
    Write-SaveResult -Ok $false -Message "docker is not available"
}

if (-not (Test-ContainerRunning -Name $Container)) {
    Write-SaveResult -Ok $true -Message "$Container is not running; nothing to send"
}

$conf = Join-Path $RepoRoot "env\dist\etc\worldserver.conf"
$soapEnabled = Get-ConfValue -Path $conf -Key "SOAP.Enabled" -Default "0"
$soapPortRaw = Get-ConfValue -Path $conf -Key "SOAP.Port" -Default "7878"
$soapIp = Get-ConfValue -Path $conf -Key "SOAP.IP" -Default "127.0.0.1"
[int]$soapPort = 7878
[void][int]::TryParse($soapPortRaw, [ref]$soapPort)

$envEnabled = (& docker exec $Container printenv AC_SOAP_ENABLED 2>$null)
if ($envEnabled) {
    $soapEnabled = $envEnabled.Trim()
}

$soapHost = "127.0.0.1"
if ($env:BONESAW_SOAP_HOST) { $soapHost = $env:BONESAW_SOAP_HOST }
elseif ($env:SOAP_HOST) { $soapHost = $env:SOAP_HOST }

$portOpen = Test-TcpOpen -TargetHost $soapHost -Port $soapPort
$wantSoap = ($soapEnabled -eq "1") -or $portOpen
$used = $null

if ($wantSoap) {
    $creds = Get-SoapCredentials -Root $RepoRoot
    if (-not $creds) {
        Write-Host "SOAP skipped: no credentials (tools/soap.env or BONESAW_SOAP_USER). Using console."
    }
    elseif (-not $portOpen) {
        Write-Host "SOAP skipped: port $soapPort not open (SOAP.Enabled=$soapEnabled, SOAP.IP=$soapIp). Using console."
    }
    else {
        $soap = Invoke-SoapCommand -TargetHost $soapHost -Port $soapPort -User $creds.User `
            -Password $creds.Password -Command $WorldCommand -ExpectText $Expect
        if ($soap.Ok) {
            $used = "SOAP"
        }
        else {
            Write-Host "$($soap.Detail). Using console."
        }
    }
}

if (-not $used) {
    if (-not (Invoke-StdinCommand -Name $Container -Root $RepoRoot -Command $WorldCommand -ExpectText $Expect)) {
        Write-SaveResult -Ok $false -Message "could not send '$WorldCommand' to $Container console"
    }
    $used = "console attach"
}

if ($WaitSeconds -gt 0) {
    Start-Sleep -Seconds $WaitSeconds
}

if ($WorldCommand -eq "saveall") {
    Write-SaveResult -Ok $true -Message "saved all players via $used; waited ${WaitSeconds}s for DB writes"
}
else {
    Write-SaveResult -Ok $true -Message "sent '$WorldCommand' via $used"
}
