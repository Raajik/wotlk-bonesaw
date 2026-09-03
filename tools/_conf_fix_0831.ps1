$ErrorActionPreference = 'Stop'
$p = 'A:\wow-bonesaw\env\dist\etc\modules\living_gear.conf'
$raw = [IO.File]::ReadAllText($p)
$lines = ($raw -replace "`r", '') -split "`n"

# 1) drop duplicate GroupKillXp lines (keep the first; values identical)
$seenGK = $false
$kept = New-Object System.Collections.Generic.List[string]
foreach ($l in $lines) {
    if ($l -match '^\s*LivingGear\.GroupKillXp\s*=') {
        if ($seenGK) { Write-Output "dedup: dropping duplicate line: $l"; continue }
        $seenGK = $true
    }
    [void]$kept.Add($l)
}

# 2) append missing keys with their code defaults (zero behavior change)
$missing = [ordered]@{
    'LivingGear.IlvlCeiling'                  = '284'
    'LivingGear.AutoTrainClassSpells'         = '1'
    'LivingGear.QuestDropAlways'              = '1'
    'LivingGear.QuestBypass.BaseCost'         = '5000000'
    'LivingGear.QuestBypass.Multiplier'       = '2.0'
    'LivingGear.QuestBypass.WindowSeconds'    = '3600'
    'LivingGear.DungeonLoot.UncommonMult'     = '4'
    'LivingGear.Quests.InfiniteDailyWeekly'   = '1'
}
$text = ($kept -join "`n").TrimEnd()
$added = @()
foreach ($k in $missing.Keys) {
    if ($text -notmatch [regex]::Escape("LivingGear.$k")) {
        $added += "$k = $($missing[$k])"
    }
}
if ($added.Count -gt 0) {
    $text = $text + "`n`n# Runtime-tuned keys documented 2026-08-31 (values = code defaults, silences Missing property warnings)`n" + ($added -join "`n")
}
$text = $text.TrimEnd() + "`n"
[IO.File]::WriteAllText($p, $text)
Write-Output "added $($added.Count) key(s): $($added -join ' | ')"
Write-Output '--- tail after fix ---'
Get-Content $p -Tail 12
