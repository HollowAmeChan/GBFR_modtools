# _step1_parse_mmat.ps1
# Input : .minfo file path (drag-dropped via bat)
# Output: output_hashes\<charname>_hashes.txt

param([string]$MinfoPath)

$scriptDir = Split-Path $PSCommandPath -Parent

function Finish {
    Write-Host ""
    Read-Host " Press Enter to close"
}

# --- validate input ---
if (-not $MinfoPath -or -not (Test-Path $MinfoPath)) {
    Write-Host " [ERROR] File not found: $MinfoPath"
    Finish; exit 1
}

$minfoDir = Split-Path $MinfoPath -Parent
$charName = (Split-Path $MinfoPath -Leaf) -replace '\.minfo$', ''
$varsDir  = Join-Path $minfoDir "vars"

Write-Host " Character : $charName"
Write-Host " minfo     : $MinfoPath"
Write-Host " vars dir  : $varsDir"

if (-not (Test-Path $varsDir)) {
    Write-Host ""
    Write-Host " [ERROR] 'vars' directory not found next to the minfo file."
    Write-Host "         Expected: $varsDir"
    Write-Host ""
    Write-Host " Make sure the full unpack is present:"
    Write-Host "   <charname>.minfo"
    Write-Host "   vars\"
    Write-Host "     0.mmat, 1.mmat, ..."
    Finish; exit 1
}

# --- find mmat files ---
$mmatFiles = Get-ChildItem "$varsDir\*.mmat" -ErrorAction SilentlyContinue |
             Sort-Object { [int]($_.BaseName) }

if ($mmatFiles.Count -eq 0) {
    Write-Host " [ERROR] No .mmat files found in: $varsDir"
    Finish; exit 1
}

Write-Host " Found     : $($mmatFiles.Count) mmat file(s)"
Write-Host ""

# --- extract Granite hashes and texture names from mmat binaries ---
# FlatBuffers stores string fields as raw ASCII; 64-char hex = Granite tile hash
$entries  = [System.Collections.Generic.List[PSObject]]::new()
$seenHash = [System.Collections.Generic.HashSet[string]]::new()
$nameRx   = [regex]'([a-z][a-z0-9_]{3,60})'
$hashRx   = [regex]'[0-9a-f]{64}'

foreach ($mmat in $mmatFiles) {
    $bytes = [System.IO.File]::ReadAllBytes($mmat.FullName)
    $text  = [System.Text.Encoding]::ASCII.GetString($bytes)

    foreach ($hm in $hashRx.Matches($text)) {
        $h = $hm.Value
        if ($seenHash.Contains($h)) { continue }
        $seenHash.Add($h) | Out-Null

        $after = $text.Substring($hm.Index + 64,
                     [Math]::Min(120, $text.Length - $hm.Index - 64))
        $nm    = $nameRx.Match($after)
        $name  = if ($nm.Success) { $nm.Value } else { "slot_$($h.Substring(0,8))" }

        $entries.Add([PSCustomObject]@{
            Hash   = $h
            Name   = $name
            Source = $mmat.Name
        })
    }
}

Write-Host " Unique Granite hashes: $($entries.Count)"

# --- write output ---
$outDir  = Join-Path $scriptDir "output_hashes"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$outFile = Join-Path $outDir "${charName}_hashes.txt"

$lines = @(
    "# GBFR Granite Texture Hash Table",
    "# Character : $charName",
    "# Generated : $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')",
    "# Source    : $MinfoPath",
    "# Total     : $($entries.Count) entries",
    "# Usage     : GraniteTextureReader.exe extract -t <.gts> -f <HASH> -l -1",
    "#",
    "# HASH                                                             NAME                                     [MMAT]",
    "# ---------------------------------------------------------------- ---------------------------------------- ------"
)
foreach ($e in $entries) {
    $lines += ("{0}  {1,-40}  [{2}]" -f $e.Hash, $e.Name, $e.Source)
}
$lines | Set-Content $outFile -Encoding ASCII

Write-Host ""
Write-Host " Hash table saved to:"
Write-Host "   $outFile"
Write-Host ""
Write-Host " Preview (first 5):"
$entries | Select-Object -First 5 |
    Format-Table -AutoSize Name, Source,
        @{Label="Hash (first 16)"; Expression={$_.Hash.Substring(0,16) + "..."}}

Finish
