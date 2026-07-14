# explore_char.ps1
# Explore all assets related to a GBFR character starting from its .minfo file
# Usage: drag a .minfo file onto explore_char.bat
# All user-visible strings are loaded from _lib/explore_strings_zh.json at runtime

param([string]$MinfoPath = "")

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$libRoot = Join-Path $PSScriptRoot "_lib"
$flatcExe = Join-Path $libRoot "flatc.exe"
$schemaFbs = Join-Path $libRoot "MMat_ModelMaterial.fbs"
. (Join-Path $libRoot "workspace_lib.ps1")

# Load Chinese strings from JSON at runtime (no parse-time encoding issues)
$stringsFile = Join-Path $libRoot "explore_strings_zh.json"
$S = ConvertFrom-Json ([IO.File]::ReadAllText($stringsFile, [Text.Encoding]::UTF8))

# Section headers via codepoints
$H_MODEL   = -join ([char]0x6A21,[char]0x578B,[char]0x5C42)
$H_MESH    = -join ([char]0x7F51,[char]0x683C,[char]0x6D41,[char]0x5F0F,[char]0x5C42)
$H_BEHAV   = -join ([char]0x884C,[char]0x4E3A,[char]0x5C42)
$H_CLOTH   = -join ([char]0x5E03,[char]0x6599,[char]0x7269,[char]0x7406,[char]0x5C42)
$H_TEX     = -join ([char]0x8D34,[char]0x56FE,[char]0x6587,[char]0x4EF6,[char]0x5C42)
$H_SUMMARY = -join ([char]0x8D44,[char]0x6E90,[char]0x603B,[char]0x89C8)


# Build hashtables from JSON values
$DESC = @{
    minfo    = $S.desc_minfo
    mmesh    = $S.desc_mmesh
    skeleton = $S.desc_skeleton
    sop      = $S.desc_sop
    mot      = $S.desc_mot
    bxm      = $S.desc_bxm
    lst      = $S.desc_lst
    cib      = $S.desc_cib
    aib      = $S.desc_aib
    clp      = $S.desc_clp
    clh      = $S.desc_clh
    rmslst   = $S.desc_rmslst
    seqcloth = $S.desc_seqcloth
}

$BXM_TYPE = @{
    facialmotion = $S.bxm_facialmotion
    cloth        = $S.bxm_cloth
    effect       = $S.bxm_effect
    flags        = $S.bxm_flags
    ik           = $S.bxm_ik
    camera       = $S.bxm_camera
    sound        = $S.bxm_sound
}

$PREFIX_DESC = @{
    pl = $S.pfx_pl; fn = $S.pfx_fn; fp = $S.pfx_fp
    np = $S.pfx_np; wp = $S.pfx_wp; em = $S.pfx_em; bg = $S.pfx_bg
}

$MMESH_DESC = @{
    pl = $S.mmesh_pl; fn = $S.mmesh_fn; fp = $S.mmesh_fp
    np = $S.mmesh_np; wp = $S.mmesh_wp
}

$MMAT_SLOT = @{
    0 = $S.mmat_0;  1 = $S.mmat_1;  2 = $S.mmat_2;  3 = $S.mmat_3
    4 = $S.mmat_4;  5 = $S.mmat_5;  6 = $S.mmat_6;  7 = $S.mmat_7
    8 = $S.mmat_8;  9 = $S.mmat_9; 10 = $S.mmat_10
}

# --- Utility functions ---

function Format-FileSize([long]$bytes) {
    if ($bytes -ge 1MB) { return "{0:F1} MB" -f ($bytes / 1MB) }
    if ($bytes -ge 1KB) { return "{0:F1} KB" -f ($bytes / 1KB) }
    return "$bytes B"
}

function Get-FileSizeStr([string]$path) {
    if (Test-Path $path) { return Format-FileSize (Get-Item $path).Length }
    return "(not found)"
}

function Extract-MmatHashes([string]$mmatPath) {
    $results = [System.Collections.Generic.List[PSCustomObject]]::new()
    try {
        $bytes = [IO.File]::ReadAllBytes($mmatPath)
        $ascii = [System.Text.Encoding]::ASCII.GetString($bytes)
        $hashMatches = [regex]::Matches($ascii, "[0-9a-f]{64}")
        foreach ($m in $hashMatches) {
            $hash       = $m.Value
            $afterStart = $m.Index + 64
            $afterLen   = [Math]::Min(200, $ascii.Length - $afterStart)
            $after      = $ascii.Substring($afterStart, $afterLen)
            $nameMatch  = [regex]::Match($after, "[a-z][a-z0-9_]{3,60}")
            $texName    = if ($nameMatch.Success) { $nameMatch.Value } else { "slot_$($hash.Substring(0,8))" }
            $results.Add([PSCustomObject]@{ Hash = $hash; Name = $texName })
        }
    } catch {}
    return $results
}

function Finish([int]$code = 0) {
    Write-Host ""
    Write-Host $S.press_any_key -ForegroundColor DarkGray
    try { $null = [Console]::ReadKey($true) } catch { }
    exit $code
}

# --- Entry validation ---

if (-not $MinfoPath -or -not (Test-Path $MinfoPath)) {
    Write-Host $S.app_title -ForegroundColor Cyan
    Write-Host $S.usage_hint -ForegroundColor Yellow
    if ($MinfoPath) { Write-Host "$($S.err_not_found) - $MinfoPath" -ForegroundColor Red }
    else            { Write-Host $S.err_no_path -ForegroundColor Red }
    Finish 1
}
$MinfoPath = (Resolve-Path $MinfoPath).Path
if ([IO.Path]::GetExtension($MinfoPath) -ne ".minfo") {
    Write-Host "$($S.err_wrong_ext) (got $([IO.Path]::GetExtension($MinfoPath)))" -ForegroundColor Red
    Finish 1
}

# --- Parse basic info ---

$charId  = [IO.Path]::GetFileNameWithoutExtension($MinfoPath)
$prefix  = [regex]::Match($charId, "^([a-z]+)").Value
$numId   = [regex]::Match($charId, "\d+").Value

$minfoDir  = [IO.Path]::GetDirectoryName($MinfoPath)
$modelDir  = [IO.Path]::GetDirectoryName($minfoDir)
$modelRoot = [IO.Path]::GetDirectoryName($modelDir)
$dataRoot  = [IO.Path]::GetDirectoryName($modelRoot)
$gameRoot  = [IO.Path]::GetDirectoryName($dataRoot)

$prefixLabel = if ($PREFIX_DESC.ContainsKey($prefix)) { $PREFIX_DESC[$prefix] } else { "Unknown ($prefix)" }

Write-Host ""
Write-Host "================================================="  -ForegroundColor Cyan
Write-Host "  GBFR $($S.app_title) -- $charId" -ForegroundColor Cyan
Write-Host "================================================="  -ForegroundColor Cyan
Write-Host "  $($S.k_gamedir) : $gameRoot" -ForegroundColor DarkGray
Write-Host "  $($S.k_pfxtype)    : $prefix ($prefixLabel)" -ForegroundColor DarkGray
Write-Host ""

# --- Output file setup ---

$scriptDir = $PSScriptRoot
$outDir    = Join-Path $scriptDir "explore_output"
if (Test-Path -LiteralPath $outDir) { Remove-Item -LiteralPath $outDir -Recurse -Force }
$sourceRoot = Join-Path $outDir "source"
$unpackRoot = Join-Path $outDir "unpack"
$buildRoot  = Join-Path $outDir "build"
New-Item -ItemType Directory -Force -Path (Join-Path $sourceRoot "data") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $unpackRoot "data") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $buildRoot "data") | Out-Null
$outMd     = Join-Path $outDir "manifest.md"
$workspaceJson = Join-Path $outDir "workspace.json"
$copyRoot  = $sourceRoot
$timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
$lines     = [System.Collections.Generic.List[string]]::new()
function L([string]$s = "") { $lines.Add($s) }

# Collect only files that the explorer actually discovers. They are copied
# after scanning so the output can preserve paths starting at data/.
$sourceFiles   = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$dataRootFull  = [IO.Path]::GetFullPath($dataRoot).TrimEnd([char[]]@('\','/'))
$dataRootPrefix = $dataRootFull + [IO.Path]::DirectorySeparatorChar

function Add-SourceFile([object]$path) {
    if ($null -eq $path) { return }
    $candidate = if ($path -is [IO.FileSystemInfo]) { $path.FullName } else { [string]$path }
    if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) { return }

    $fullPath = [IO.Path]::GetFullPath($candidate)
    if ($fullPath.StartsWith($dataRootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        $sourceFiles.Add($fullPath) | Out-Null
    }
}

function Add-SourceFiles([object[]]$files) {
    foreach ($file in @($files)) { Add-SourceFile $file }
}

function Copy-DiscoveredSources {
    $dataOut = Join-Path $copyRoot "data"
    $copied = 0
    $totalBytes = [long]0
    $failed = [System.Collections.Generic.List[string]]::new()

    foreach ($source in ($sourceFiles | Sort-Object)) {
        $relative = $source.Substring($dataRootPrefix.Length)
        $dest = Join-Path $dataOut $relative
        $destDir = [IO.Path]::GetDirectoryName($dest)
        if (-not (Test-Path -LiteralPath $destDir)) {
            New-Item -ItemType Directory -Force -Path $destDir | Out-Null
        }

        try {
            [IO.File]::Copy($source, $dest, $true)
            $copied++
            $totalBytes += (Get-Item -LiteralPath $source).Length
        } catch {
            $failed.Add("$source -- $($_.Exception.Message)")
        }
    }

    return [PSCustomObject]@{
        Copied     = $copied
        TotalBytes = $totalBytes
        Failed     = $failed
    }
}

function Initialize-WorkspaceArtifacts {
    $textures = [System.Collections.Generic.List[PSCustomObject]]::new()
    $materials = [System.Collections.Generic.List[PSCustomObject]]::new()
    $errors = [System.Collections.Generic.List[string]]::new()

    foreach ($source in ($sourceFiles | Sort-Object)) {
        $relativeDataPath = $source.Substring($dataRootPrefix.Length)
        $sourceRelative = ConvertTo-WorkspacePath (Join-Path "source\data" $relativeDataPath)
        $sourceCopy = Resolve-WorkspaceFile $outDir $sourceRelative

        if ([IO.Path]::GetExtension($source) -ieq ".texture") {
            try {
                $relativeDir = [IO.Path]::GetDirectoryName($relativeDataPath)
                $unpackDir = Join-Path (Join-Path $unpackRoot "data") $relativeDir
                $slots = @(Expand-WtbTexture $sourceCopy $unpackDir)
                $slotRecords = @($slots | ForEach-Object {
                    [PSCustomObject]@{
                        Index = $_.Index
                        Path = ConvertTo-WorkspacePath ($_.Path.Substring($outDir.Length + 1))
                        BaselineSha256 = $_.Sha256
                    }
                })
                $textures.Add([PSCustomObject]@{
                    Source = $sourceRelative
                    Output = ConvertTo-WorkspacePath (Join-Path "build\data" $relativeDataPath)
                    SourceSha256 = Get-WorkspaceSha256 $sourceCopy
                    Slots = $slotRecords
                })
            } catch {
                $errors.Add("texture: $relativeDataPath -- $($_.Exception.Message)")
            }
        } elseif ([IO.Path]::GetExtension($source) -ieq ".mmat") {
            try {
                if (-not (Test-Path -LiteralPath $flatcExe)) { throw "flatc.exe not found" }
                if (-not (Test-Path -LiteralPath $schemaFbs)) { throw "MMat_ModelMaterial.fbs not found" }
                $jsonRelative = ConvertTo-WorkspacePath (Join-Path "unpack\data" ($relativeDataPath + ".json"))
                $jsonPath = Resolve-WorkspaceFile $outDir $jsonRelative
                Convert-MmatToJson $flatcExe $schemaFbs $sourceCopy $jsonPath | Out-Null
                $materials.Add([PSCustomObject]@{
                    Source = $sourceRelative
                    Json = $jsonRelative
                    Output = ConvertTo-WorkspacePath (Join-Path "build\data" $relativeDataPath)
                    BaselineSha256 = Get-WorkspaceSha256 $jsonPath
                })
            } catch {
                $errors.Add("mmat: $relativeDataPath -- $($_.Exception.Message)")
            }
        }
    }

    $workspace = [ordered]@{
        Version = 1
        CharacterId = $charId
        GeneratedAt = (Get-Date).ToString("o")
        Manifest = "manifest.md"
        SourceRoot = "source"
        UnpackRoot = "unpack"
        BuildRoot = "build"
        Textures = @($textures)
        Materials = @($materials)
        DecodeErrors = @($errors)
    }
    $json = $workspace | ConvertTo-Json -Depth 8
    [IO.File]::WriteAllText($workspaceJson, $json, [System.Text.UTF8Encoding]::new($false))

    return [PSCustomObject]@{
        TextureCount = $textures.Count
        MaterialCount = $materials.Count
        ErrorCount = $errors.Count
        Errors = $errors
    }
}

# Markdown header
L "# $charId $($S.title_suffix)"
L ""
L "| $($S.col_field) | $($S.col_value) |"
L "|---|---|"
L "| **$($S.k_gentime)** | $timestamp |"
L "| **$($S.k_charid)** | $charId |"
L "| **$($S.k_numid)** | $numId |"
L "| **$($S.k_pfxtype)** | $prefix -- $prefixLabel |"
L "| **$($S.k_gamedir)** | $gameRoot |"
L ""
L "---"

# ================================================================
# Model Layer
# ================================================================
L ""
L "## $H_MODEL"

function Scan-ModelDir([string]$dp, [string]$dc, [string]$sectionLabel) {
    $dir = Join-Path $gameRoot "data\model\$dp\$dc"
    L ""
    L "### $sectionLabel   ``$dp/$dc/``"
    if (-not (Test-Path $dir)) {
        L ""
        L "> $($S.model_not_found): ``$dir``"
        Write-Host "  [skip] $dp/$dc (not found)" -ForegroundColor DarkYellow
        return
    }
    Write-Host "  [scan] data/model/$dp/$dc" -ForegroundColor Gray

    L ""
    L "| $($S.col_file) | $($S.col_size) | $($S.col_desc) |"
    L "|------|------|-------------|"
    foreach ($ext in @(".minfo",".mmesh",".skeleton",".sop")) {
        $f = Join-Path $dir "$dc$ext"
        if (Test-Path $f) {
            Add-SourceFile $f
            $sz     = Format-FileSize (Get-Item $f).Length
            $extKey = $ext.TrimStart(".")
            $d      = if ($DESC.ContainsKey($extKey)) { $DESC[$extKey] } else { $ext }
            L "| ``$dc$ext`` | $sz | $d |"
        }
    }

    $varsDir = Join-Path $dir "vars"
    if (Test-Path $varsDir) {
        $mmats = Get-ChildItem $varsDir -Filter "*.mmat" | Sort-Object Name
        foreach ($mmat in $mmats) {
            Add-SourceFile $mmat
            $idx  = try { [int][IO.Path]::GetFileNameWithoutExtension($mmat.Name) } catch { -1 }
            $sz   = Format-FileSize $mmat.Length
            $slot = if ($MMAT_SLOT.ContainsKey($idx)) { $MMAT_SLOT[$idx] } else { "$($S.mat_variant) $idx" }
            L "| ``vars/$($mmat.Name)`` | $sz | $($S.mat_variant) $idx -- $slot |"
        }

        L ""
        L "$($S.tex_ref_title)"
        L ""
        L "| $($S.col_hash16) | $($S.col_fullhash) | $($S.col_texname) | $($S.col_srcmmat) |"
        L "|---|---|---|---|"
        $seen = [System.Collections.Generic.HashSet[string]]::new()
        foreach ($mmat in $mmats) {
            $hashes = Extract-MmatHashes $mmat.FullName
            foreach ($h in $hashes) {
                if ($seen.Add($h.Hash)) {
                    L "| ``$($h.Hash.Substring(0,16))`` | ``$($h.Hash)`` | ``$($h.Name)`` | ``$($mmat.Name)`` |"
                }
            }
        }
        L ""
        L "> **$($S.note_granite_1)**"
        L "> $($S.note_granite_2)"
        L "> $($S.note_granite_3)"
        L "> **$($S.note_granite_4)**"
        L "> $($S.note_granite_5)"
    }

    $extras = Get-ChildItem $dir -File | Where-Object {
        $_.Extension -notin @(".minfo",".mmesh",".skeleton",".sop") -and $_.Name -ne "Thumbs.db"
    }
    if ($extras) {
        L ""
        L "$($S.tex_other_files)"
        L ""
        L "| $($S.col_file) | $($S.col_size) | $($S.col_desc) |"
        L "|------|------|------|"
        foreach ($e in $extras) {
            Add-SourceFile $e
            $extKey = $e.Extension.TrimStart(".")
            $d = if ($DESC.ContainsKey($extKey)) { $DESC[$extKey] } else { $e.Extension }
            L "| ``$($e.Name)`` | $(Format-FileSize $e.Length) | $d |"
        }
    }
}

Scan-ModelDir $prefix $charId $S.sec_main
if ($prefix -eq "pl") {
    Scan-ModelDir "fn" "fn$numId" $S.sec_fn
    Scan-ModelDir "fp" "fp$numId" $S.sec_fp
    Scan-ModelDir "np" "np$numId" $S.sec_np
    Scan-ModelDir "wp" "wp$numId" $S.sec_wp
}

# ================================================================
# Mesh Streaming
# ================================================================
L ""
L "---"
L ""
L "## $H_MESH"
L ""
L "$($S.tex_path_note -replace "data/texture.*","model_streaming/lod{0-3}/")"
L ""
L "| $($S.col_lod) | $($S.col_file) | $($S.col_size) | $($S.col_desc) |"
L "|-----|------|------|-------------|"

$lodRoot = Join-Path $dataRoot "model_streaming"
if (Test-Path $lodRoot) {
    foreach ($lodDir in (Get-ChildItem $lodRoot -Directory | Sort-Object Name)) {
        $lod         = $lodDir.Name
        $relPrefixes = @($charId)
        if ($prefix -eq "pl") { $relPrefixes += @("fn$numId","fp$numId","np$numId","wp$numId") }
        foreach ($rp in $relPrefixes) {
            $f = Join-Path $lodDir.FullName "$rp.mmesh"
            if (Test-Path $f) {
                Add-SourceFile $f
                $sz    = Format-FileSize (Get-Item $f).Length
                $rpPfx = [regex]::Match($rp, "^[a-z]+").Value
                $d     = if ($MMESH_DESC.ContainsKey($rpPfx)) { $MMESH_DESC[$rpPfx] } else { "$rpPfx mesh" }
                L "| $lod | ``$rp.mmesh`` | $sz | $d |"
            }
        }
    }
    Write-Host "  [scan] model_streaming/lod*" -ForegroundColor Gray
} else {
    L "| -- | (model_streaming not found) | -- | -- |"
}

# ================================================================
# Behavior Layer (pl only)
# ================================================================
if ($prefix -eq "pl") {
    $plDir = Join-Path $gameRoot "data\pl\$charId"
    L ""
L "---"
L ""
    L "## $H_BEHAV"
L ""
    L "path: ``data/pl/$charId/``"
L ""

    if (Test-Path $plDir) {
        Write-Host "  [scan] data/pl/$charId" -ForegroundColor Gray
        $motFiles   = @(Get-ChildItem $plDir -Filter "*.mot"  -File)
        $bxmFiles   = @(Get-ChildItem $plDir -Filter "*.bxm"  -File)
        $lstFiles   = @(Get-ChildItem $plDir -Filter "*.lst"  -File)
        $otherFiles = @(Get-ChildItem $plDir -File | Where-Object { $_.Extension -notin @(".mot",".bxm",".lst") })
        Add-SourceFiles $motFiles
        Add-SourceFiles $bxmFiles
        Add-SourceFiles $lstFiles
        Add-SourceFiles $otherFiles

        L "| $($S.col_type) | $($S.col_count) | $($S.col_total) | $($S.col_desc) |"
        L "|------|-------|----------|-------------|"
        if ($motFiles.Count -gt 0) {
            $sz = ($motFiles | Measure-Object Length -Sum).Sum
            L "| ``.mot`` | $($motFiles.Count) | $(Format-FileSize $sz) | $($DESC["mot"]) |"
        }
        if ($bxmFiles.Count -gt 0) {
            $sz = ($bxmFiles | Measure-Object Length -Sum).Sum
            L "| ``.bxm`` | $($bxmFiles.Count) | $(Format-FileSize $sz) | $($DESC["bxm"]) |"
        }
        foreach ($lst in $lstFiles) {
            L "| ``$($lst.Name)`` | 1 | $(Format-FileSize $lst.Length) | $($DESC["lst"]) |"
        }
        foreach ($f in $otherFiles) {
            $extKey = $f.Extension.TrimStart(".")
            $d = if ($DESC.ContainsKey($extKey)) { $DESC[$extKey] } else { $f.Extension }
            L "| ``$($f.Name)`` | 1 | $(Format-FileSize $f.Length) | $d |"
        }
        if ($bxmFiles.Count -gt 0) {
            L ""
            L "$($S.bxm_breakdown)"
            L ""
            L "| $($S.col_suffix) | $($S.col_count) | $($S.col_meaning) |"
            L "|---|---|---|"
            $bxmGroups = $bxmFiles | Group-Object {
                $m = [regex]::Match($_.Name, "_seq_edit_([a-z]+)\.bxm$")
                if ($m.Success) { $m.Groups[1].Value } else { "other" }
            } | Sort-Object Count -Descending
            foreach ($g in $bxmGroups) {
                $sfx   = $g.Name
                $desc2 = if ($BXM_TYPE.ContainsKey($sfx)) { $BXM_TYPE[$sfx] } else { "$($S.bxm_other)" }
                $pat   = if ($sfx -ne "other") { "*_seq_edit_$sfx.bxm" } else { "(other)" }
                L "| ``$pat`` | $($g.Count) | $desc2 |"
            }
        }

        # Mot ID range analysis
        if ($motFiles.Count -gt 0) {
            $MOT_CAT = @{
                0x00 = $S.mot_cat_0000; 0x05 = $S.mot_cat_0500
                0x06 = $S.mot_cat_0600; 0x0a = $S.mot_cat_0a00
                0x0b = $S.mot_cat_0b00; 0x18 = $S.mot_cat_1800
                0x30 = $S.mot_cat_3000; 0x31 = $S.mot_cat_3100
                0x32 = $S.mot_cat_3200; 0x33 = $S.mot_cat_3300
                0x34 = $S.mot_cat_3400; 0x35 = $S.mot_cat_3500
                0x3a = $S.mot_cat_3a00; 0x3f = $S.mot_cat_3f00
                0xa5 = $S.mot_cat_a500; 0xb0 = $S.mot_cat_b000
                0xb5 = $S.mot_cat_b500; 0xb6 = $S.mot_cat_b600
                0xba = $S.mot_cat_ba00; 0xbb = $S.mot_cat_bb00
                0xe3 = $S.mot_cat_e300; 0xe4 = $S.mot_cat_e400
                0xea = $S.mot_cat_ea00; 0xeb = $S.mot_cat_eb00
                0xec = $S.mot_cat_ec00; 0xed = $S.mot_cat_ed00
                0xfa = $S.mot_cat_fa00
            }

            L ""
            L "$($S.mot_id_breakdown)"
            L ""
            L "| ID范围 | 数量 | 总KB | 分类推断 |"
            L "|---|---|---|---|"

            $motGroups = $motFiles | Group-Object {
                $m = [regex]::Match($_.BaseName, "_([0-9a-f]{4})$")
                if ($m.Success) { "0x{0:x2}" -f [Convert]::ToInt32($m.Groups[1].Value.Substring(0,2),16) }
                else { "other" }
            } | Sort-Object {
                if ($_.Name -eq "other") { 0xffff }
                else { [Convert]::ToInt32(($_.Name -replace '^0x',''), 16) }
            }

            foreach ($g in $motGroups) {
                $hiKey  = if ($g.Name -eq "other") { -1 } else { [Convert]::ToInt32(($g.Name -replace '^0x',''), 16) }
                $cat    = if ($MOT_CAT.ContainsKey($hiKey)) { $MOT_CAT[$hiKey] } else { $S.mot_cat_unknown }
                $kb     = [int](($g.Group | Measure-Object Length -Sum).Sum / 1024)
                $sorted = @($g.Group | Sort-Object Name)
                $id0    = [regex]::Match($sorted[0].BaseName,   "_([0-9a-f]{4})$").Groups[1].Value
                $id1    = [regex]::Match($sorted[-1].BaseName,  "_([0-9a-f]{4})$").Groups[1].Value
                L "| ``$($g.Name)xx`` ($id0~$id1) | $($g.Count) | $kb | $cat |"
            }

            L ""
            L "> $($S.mot_note_ids)"
            L "> $($S.mot_note_sba)"
            L "> $($S.mot_note_ed)"

            L ""
            L "$($S.mot_top_largest)"
            L ""
            L "| $($S.col_file) | $($S.col_size) | 分类 |"
            L "|---|---|---|"
            $top10 = $motFiles | Sort-Object Length -Descending | Select-Object -First 10
            foreach ($t in $top10) {
                $m2   = [regex]::Match($t.BaseName, "_([0-9a-f]{4})$")
                $cat2 = if ($m2.Success) {
                    $hi2 = [Convert]::ToInt32($m2.Groups[1].Value.Substring(0,2),16)
                    if ($MOT_CAT.ContainsKey($hi2)) { $MOT_CAT[$hi2] } else { $S.mot_cat_unknown }
                } else { $S.mot_cat_unknown }
                L "| ``$($t.Name)`` | $(Format-FileSize $t.Length) | $cat2 |"
            }
        }

        # Cloth Physics
        $clothDir = Join-Path $plDir "cloth"
        L ""
L "---"
L ""
        L "## $H_CLOTH"
L ""
        L "path: ``data/pl/$charId/cloth/``"
L ""

        if (Test-Path $clothDir) {
            Write-Host "  [scan] cloth/" -ForegroundColor Gray
            $clothFiles = Get-ChildItem $clothDir -File | Where-Object { $_.Extension -ne ".xml" } | Sort-Object Name
            L "| $($S.col_file) | $($S.col_size) | $($S.col_desc) |"
            L "|------|------|------|"
            foreach ($cf in $clothFiles) {
                Add-SourceFile $cf
                $sz = Format-FileSize $cf.Length
                $d = switch -Regex ($cf.Name) {
                    "_clp\.bxm$"   { $DESC["clp"] }
                    "_clh\.bxm$"   { $DESC["clh"] }
                    "rmslst\.bxm$" { $DESC["rmslst"] }
                    "cloth\.bxm$"  { $DESC["seqcloth"] }
                    default          { $cf.Extension }
                }
                $gn = [regex]::Match($cf.Name, "_0_(\d+)_").Groups[1].Value
                if ($gn) { $d += " ($($S.group_label) $gn)" }
                L "| ``$($cf.Name)`` | $sz | $d |"
            }
            $clpCnt = @($clothFiles | Where-Object { $_.Name -match "_clp\.bxm$" }).Count
            $clhCnt = @($clothFiles | Where-Object { $_.Name -match "_clh\.bxm$" }).Count
            L ""
            L "> **$($S.cloth_groups)**: ``_clp`` x $clpCnt ,  ``_clh`` x $clhCnt"
            L "> $($S.cloth_convert)"
        } else {
            L "> $($S.cloth_not_found): ``$clothDir``"
        }
    } else {
        L "> $($S.behav_not_found): ``$plDir``"
    }
}

# ================================================================
# Texture Files
# ================================================================
$texRoot = Join-Path $dataRoot "texture"
L ""
L "---"
L ""
L "## $H_TEX"
L ""
L "$($S.tex_path_note)"
L ""

$MSK_DESC = @{
    msk2 = $S.msk_msk2; msk3 = $S.msk_msk3
    msk4 = $S.msk_msk4; msk5 = $S.msk_msk5
}

$searchIds = @($charId)
if ($prefix -eq "pl") { $searchIds += @("fn$numId","fp$numId","np$numId","wp$numId") }

if (Test-Path $texRoot) {
    Write-Host "  [scan] data/texture/" -ForegroundColor Gray
    $allTex = [System.Collections.Generic.List[PSCustomObject]]::new()
    foreach ($res in @("2k","4k")) {
        $resDir = Join-Path $texRoot $res
        if (-not (Test-Path $resDir)) { continue }
        foreach ($sid in $searchIds) {
            $files = Get-ChildItem $resDir -Filter "*$sid*.texture" -File -ErrorAction SilentlyContinue
            foreach ($f in $files) {
                Add-SourceFile $f
                $allTex.Add([PSCustomObject]@{
                    Res      = $res
                    Name     = $f.Name
                    BaseName = [IO.Path]::GetFileNameWithoutExtension($f.Name)
                    Size     = $f.Length
                })
            }
        }
    }
    if ($allTex.Count -eq 0) {
        L "> $($S.tex_not_found) $charId"
    } else {
        foreach ($res in @("2k","4k")) {
            $group = @($allTex | Where-Object { $_.Res -eq $res })
            if ($group.Count -eq 0) { continue }
            L ""
            L "### $($S.tex_res): $res"
            L ""
            L "| $($S.col_file) | $($S.col_size) | $($S.col_slot) | $($S.col_desc) |"
            L "|------|------|------|------|"
            foreach ($t in ($group | Sort-Object Name)) {
                $sz        = Format-FileSize $t.Size
                $slotMatch = [regex]::Match($t.BaseName, "_(msk\d+)$")
                $slot      = if ($slotMatch.Success) { $slotMatch.Groups[1].Value } else { "?" }
                $slotDesc  = if ($MSK_DESC.ContainsKey($slot)) { $MSK_DESC[$slot] } else { $slot }
                L "| ``$($t.Name)`` | $sz | ``$slot`` | $slotDesc |"
            }
        }
        $names2k = @($allTex | Where-Object { $_.Res -eq "2k" } | ForEach-Object { $_.Name })
        $names4k = @($allTex | Where-Object { $_.Res -eq "4k" } | ForEach-Object { $_.Name })
        $only2k  = @($names2k | Where-Object { $names4k -notcontains $_ })
        $only4k  = @($names4k | Where-Object { $names2k -notcontains $_ })
        if ($only2k.Count -gt 0 -or $only4k.Count -gt 0) {
            L ""
            L "> **$($S.tex_note_diff_res)**"
            foreach ($n in $only2k) { L "> - ``$n`` $($S.tex_only2k)" }
            foreach ($n in $only4k) { L "> - ``$n`` $($S.tex_only4k)" }
        }
        L ""
        $totalUniq = @($allTex | Select-Object -ExpandProperty Name -Unique).Count
        L "> **$($S.tex_total)**: $totalUniq  (x2 = $($allTex.Count))"
        L "> $($S.tex_replace_note)"
        L ""
        L "$($S.mod_approach_title)"
        L ""
        L "| $($S.col_approach) | $($S.col_when) | $($S.col_steps) |"
        L "|---|---|---|"
        L "| **$($S.mod_A_name)** | $($S.mod_A_when) | $($S.mod_A_steps) |"
        L "| **$($S.mod_B_name)** | $($S.mod_B_when) | $($S.mod_B_steps) |"
        L "| **$($S.mod_C_name)** | $($S.mod_C_when) | $($S.mod_C_steps) |"
    }
} else {
    L "> $($S.tex_not_found): ``$texRoot``"
}

$copyResult = Copy-DiscoveredSources
$artifactResult = Initialize-WorkspaceArtifacts

# ================================================================
# Summary
# ================================================================
L ""
L "---"
L ""
L "## $H_SUMMARY"
L ""
L "| $($S.col_field) | $($S.col_value) |"
L "|---|---|"
L "| **$($S.workspace_path)** | ``$outDir`` |"
L "| **$($S.copy_count)** | $($copyResult.Copied) |"
L "| **$($S.copy_total_size)** | $(Format-FileSize $copyResult.TotalBytes) |"
L "| **$($S.decoded_textures)** | $($artifactResult.TextureCount) |"
L "| **$($S.decoded_materials)** | $($artifactResult.MaterialCount) |"
L "| **$($S.decode_failed)** | $($artifactResult.ErrorCount) |"
if ($copyResult.Failed.Count -gt 0) {
    L "| **$($S.copy_failed)** | $($copyResult.Failed.Count) |"
    L ""
    foreach ($failure in $copyResult.Failed) { L "> $failure" }
}
L ""
L "---"
L "*$($S.generated_by)*"

# ================================================================
# Write Markdown + Console preview
# ================================================================
$mdContent = $lines -join "`n"
[IO.File]::WriteAllText($outMd, $mdContent, [System.Text.UTF8Encoding]::new($true))

Write-Host ""
Write-Host $S.preview_title -ForegroundColor Cyan
Write-Host ""

$modelDirMain = Join-Path $gameRoot "data\model\$prefix\$charId"
if (Test-Path $modelDirMain) {
    Write-Host $S.model_layer -ForegroundColor Yellow
    Write-Host ("  {0,-40} {1,10}" -f "$charId.minfo", (Get-FileSizeStr (Join-Path $modelDirMain "$charId.minfo")))
    $vd = Join-Path $modelDirMain "vars"
    $mc = @(Get-ChildItem $vd -Filter "*.mmat" -ErrorAction SilentlyContinue).Count
    Write-Host ("  vars/*.mmat  ($mc $($S.variants_count))")
}

if ($prefix -eq "pl") {
    Write-Host ""
    Write-Host $S.mesh_layer -ForegroundColor Yellow
    $lod0 = Join-Path $dataRoot "model_streaming\lod0"
    foreach ($mp in @($charId,"fn$numId","fp$numId","np$numId","wp$numId")) {
        $f = Join-Path $lod0 "$mp.mmesh"
        if (Test-Path $f) {
            Write-Host ("  {0,-40} {1,10}" -f "$mp.mmesh", (Format-FileSize (Get-Item $f).Length))
        }
    }

    $plDir2 = Join-Path $gameRoot "data\pl\$charId"
    if (Test-Path $plDir2) {
        Write-Host ""
        Write-Host $S.behav_layer -ForegroundColor Yellow
        $mc2  = @(Get-ChildItem $plDir2 -Filter "*.mot" -File).Count
        $bxmc = @(Get-ChildItem $plDir2 -Filter "*.bxm" -File).Count
        Write-Host "  $($S.anim_count) : $mc2"
        Write-Host "  $($S.seq_count)  : $bxmc"
        $cd2 = Join-Path $plDir2 "cloth"
        if (Test-Path $cd2) {
            $cpc = @(Get-ChildItem $cd2 -Filter "*_clp.bxm").Count
            Write-Host "  $($S.cloth_count_label)    : $cpc"
        }
    }
}

Write-Host ""
Write-Host $S.report_saved -ForegroundColor Green
Write-Host "  $outMd" -ForegroundColor Green
Write-Host ""
Write-Host "$($S.copy_done): $($copyResult.Copied) $($S.copy_files) ($(Format-FileSize $copyResult.TotalBytes))" -ForegroundColor Green
Write-Host "  $sourceRoot" -ForegroundColor Green
if ($copyResult.Failed.Count -gt 0) {
    Write-Host "$($S.copy_failed): $($copyResult.Failed.Count)" -ForegroundColor Yellow
}
Write-Host "$($S.decode_done): texture $($artifactResult.TextureCount), mmat $($artifactResult.MaterialCount)" -ForegroundColor Green
Write-Host "  $unpackRoot" -ForegroundColor Green
if ($artifactResult.ErrorCount -gt 0) {
    Write-Host "$($S.decode_failed): $($artifactResult.ErrorCount)" -ForegroundColor Yellow
}
Write-Host "$($S.workspace_ready)" -ForegroundColor Green
Write-Host "  $outDir" -ForegroundColor Green
Write-Host ""

Finish 0
