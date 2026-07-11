# _step2_extract_tex.ps1
# Input : .gts file path (drag-dropped via bat)
# Reads : output_hashes\*_hashes.txt  (produced by step 1)
# Output: output_textures\<charname>\

param([string]$GtsPath)

$scriptDir  = Split-Path $PSCommandPath -Parent
$graniteExe = Join-Path $scriptDir "GraniteTextureReader.exe"
$hashesDir  = Join-Path $scriptDir "output_hashes"

function Finish {
    Write-Host ""
    Read-Host " Press Enter to close"
}

# --- validate GraniteTextureReader ---
if (-not (Test-Path $graniteExe)) {
    Write-Host ""
    Write-Host " [ERROR] GraniteTextureReader.exe not found at:"
    Write-Host "   $graniteExe"
    Write-Host ""
    Write-Host " Download from:"
    Write-Host "   https://github.com/Nenkai/GraniteTextureReader/releases"
    Write-Host " Then place GraniteTextureReader.exe in this folder."
    Finish; exit 1
}

# --- validate gts file ---
if (-not $GtsPath -or -not (Test-Path $GtsPath)) {
    Write-Host " [ERROR] GTS file not found: $GtsPath"
    Finish; exit 1
}

Write-Host ""
Write-Host " GTS file  : $GtsPath"

# --- find hash table(s) ---
$hashFiles = Get-ChildItem "$hashesDir\*_hashes.txt" -ErrorAction SilentlyContinue |
             Sort-Object LastWriteTime -Descending

if ($hashFiles.Count -eq 0) {
    Write-Host ""
    Write-Host " [ERROR] No hash table found in: $hashesDir"
    Write-Host ""
    Write-Host " Run step 1 first: drag a .minfo file onto 01_minfo转Hash表.bat"
    Finish; exit 1
}

# if multiple hash tables exist, let user pick
if ($hashFiles.Count -gt 1) {
    Write-Host ""
    Write-Host " Multiple hash tables found:"
    for ($i = 0; $i -lt $hashFiles.Count; $i++) {
        $age    = (Get-Date) - $hashFiles[$i].LastWriteTime
        $tag    = if ($i -eq 0) { " <- most recent" } else { "" }
        $ageStr = if ($age.TotalHours -lt 1)   { "$([int]$age.TotalMinutes)m ago" }
                  elseif ($age.TotalDays -lt 1) { "$([int]$age.TotalHours)h ago" }
                  else                          { "$([int]$age.TotalDays)d ago" }
        Write-Host ("   [{0}] {1,-40}  ({2}){3}" -f $i, $hashFiles[$i].Name, $ageStr, $tag)
    }
    Write-Host ""
    $raw      = Read-Host " Enter number (Enter = most recent [0])"
    $idx      = if ($raw -match '^\d+$' -and [int]$raw -lt $hashFiles.Count) { [int]$raw } else { 0 }
    $hashFile = $hashFiles[$idx]
} else {
    $hashFile = $hashFiles[0]
}

$charName = $hashFile.BaseName -replace '_hashes$', ''
Write-Host " Hash table: $($hashFile.Name)  (character: $charName)"

# --- read hash entries (skip comment lines) ---
$entries = Get-Content $hashFile |
           Where-Object { $_ -notmatch '^\s*#' -and $_ -match '[0-9a-f]{64}' } |
           ForEach-Object {
               $parts = $_ -split '\s+'
               [PSCustomObject]@{ Hash = $parts[0]; Name = $parts[1] }
           }

if ($entries.Count -eq 0) {
    Write-Host " [ERROR] Hash table is empty or malformed: $($hashFile.FullName)"
    Finish; exit 1
}

Write-Host " Entries   : $($entries.Count)"

# --- prepare output directory ---
$outDir = Join-Path $scriptDir "output_textures\$charName"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
Write-Host " Output    : $outDir"
Write-Host ""

# --- batch extract ---
$success = 0
$fail    = 0
$total   = $entries.Count
$tmpErr  = Join-Path $env:TEMP "gtr_err_tmp.txt"

foreach ($entry in $entries) {
    $idx = $success + $fail + 1
    Write-Progress -Activity "Extracting textures" `
                   -Status ("[{0}/{1}] {2}" -f $idx, $total, $entry.Name) `
                   -PercentComplete ([int](($idx / $total) * 100))

    $proc = Start-Process -FilePath $graniteExe `
        -ArgumentList @("extract",
                        "-t", "`"$GtsPath`"",
                        "-f", $entry.Hash,
                        "-l", "-1",
                        "-o", "`"$outDir`"") `
        -Wait -PassThru -NoNewWindow `
        -RedirectStandardError $tmpErr

    if ($proc.ExitCode -eq 0) {
        $success++
        Write-Host ("  OK  [{0}/{1}]  {2}" -f $idx, $total, $entry.Name) -ForegroundColor Green
    } else {
        $fail++
        $err = (Get-Content $tmpErr -Raw -ErrorAction SilentlyContinue) -replace "`n", " "
        Write-Host ("  !!  [{0}/{1}]  {2}  =>  {3}" -f $idx, $total, $entry.Name, $err) -ForegroundColor Yellow
    }
}

Write-Progress -Activity "Extracting textures" -Completed

Write-Host ""
if ($fail -eq 0) {
    Write-Host (" Done. {0} texture(s) extracted." -f $success) -ForegroundColor Green
} else {
    Write-Host (" Done. OK: {0}  Failed: {1}" -f $success, $fail) -ForegroundColor Yellow
    Write-Host " Failed hashes may not exist in this .gts file."
    Write-Host " Try other gts files: granite\2k\gts\0\, granite\4k\gts\, etc."
}
Write-Host ""
Write-Host " Output folder:"
Write-Host "   $outDir"

Finish
