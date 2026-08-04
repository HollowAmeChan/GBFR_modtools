param(
    [Parameter(Mandatory = $true)][string]$WorkspacePath,
    [switch]$DryRun
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
. (Join-Path $PSScriptRoot 'workspace_lib.ps1')
. (Join-Path $PSScriptRoot 'ui_asset_rules.ps1')

function Get-CategoryLabel([object]$labels, [string]$key) {
    $property = $labels.PSObject.Properties[$key]
    if ($null -ne $property) { return [string]$property.Value }
    return [string]$labels.default
}

$workspaceRoot = [IO.Path]::GetFullPath($WorkspacePath).TrimEnd([char[]]@('\', '/'))
$workspaceJson = Join-Path $workspaceRoot 'workspace.json'
if (-not (Test-Path -LiteralPath $workspaceJson -PathType Leaf)) {
    throw "workspace.json not found: $workspaceJson"
}

$workspace = ConvertFrom-Json ([IO.File]::ReadAllText($workspaceJson, [Text.Encoding]::UTF8))
$characterId = [string]$workspace.CharacterId
$gameDataRoot = [IO.Path]::GetFullPath([string]$workspace.GameDataRoot).TrimEnd([char[]]@('\', '/'))
$gameUiRoot = Join-Path $gameDataRoot 'ui'
if (-not (Test-Path -LiteralPath $gameUiRoot -PathType Container)) {
    throw "Game UI root not found: $gameUiRoot"
}

$labelsPath = Join-Path $repoRoot '_lib\ui_asset_categories_zh.json'
$labels = ConvertFrom-Json ([IO.File]::ReadAllText($labelsPath, [Text.Encoding]::UTF8))
$existingRecords = @($workspace.UIImages)
$existingSources = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($record in $existingRecords) { $existingSources.Add([string]$record.Source) | Out-Null }

$candidates = @(Get-ChildItem -LiteralPath $gameUiRoot -Recurse -File -Filter '*.wtb' | ForEach-Object {
    $relativeDataPath = $_.FullName.Substring($gameDataRoot.Length + 1).Replace('\', '/')
    $categoryKey = Get-CharacterUiAssetCategory -RelativePath $relativeDataPath -CharacterId $characterId
    if ($categoryKey) {
        $sourceRelative = ConvertTo-WorkspacePath (Join-Path 'source\data' $relativeDataPath)
        if (-not $existingSources.Contains($sourceRelative)) {
            [PSCustomObject]@{
                File = $_
                RelativeDataPath = $relativeDataPath
                Source = $sourceRelative
                CategoryKey = $categoryKey
            }
        }
    }
} | Sort-Object RelativeDataPath)

$candidateBytes = ($candidates | ForEach-Object { $_.File.Length } | Measure-Object -Sum).Sum
if ($null -eq $candidateBytes) { $candidateBytes = 0 }
Write-Host "CharacterId=$characterId"
Write-Host "ExistingUIImages=$($existingRecords.Count)"
Write-Host "NewUIImages=$($candidates.Count)"
Write-Host ("NewSourceMiB={0:F1}" -f ($candidateBytes / 1MB))
if ($DryRun) {
    $candidates | Group-Object CategoryKey | Sort-Object Name | ForEach-Object {
        Write-Host "Category.$($_.Name)=$($_.Count)"
    }
    exit 0
}

if ($candidates.Count -eq 0) {
    Write-Host 'No new character UI assets were found.'
    exit 0
}

$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$backupDir = Join-Path $workspaceRoot '.gbfr\backfill'
$temporaryRoot = Join-Path $workspaceRoot ".gbfr\ui-backfill-$stamp"
New-Item -ItemType Directory -Force -Path $backupDir | Out-Null
New-Item -ItemType Directory -Force -Path $temporaryRoot | Out-Null
$backupPath = Join-Path $backupDir "workspace.$stamp.json"

$newRecords = [Collections.Generic.List[object]]::new()
$preservedSources = 0
$preservedDds = 0
try {
    foreach ($candidate in $candidates) {
        $sourceTarget = Resolve-WorkspaceFile $workspaceRoot $candidate.Source
        $sourceDirectory = [IO.Path]::GetDirectoryName($sourceTarget)
        New-Item -ItemType Directory -Force -Path $sourceDirectory | Out-Null
        if (Test-Path -LiteralPath $sourceTarget -PathType Leaf) {
            $preservedSources++
        } else {
            [IO.File]::Copy($candidate.File.FullName, $sourceTarget, $false)
        }

        $relativeDirectory = [IO.Path]::GetDirectoryName($candidate.RelativeDataPath)
        $temporaryDirectory = Join-Path $temporaryRoot ([Guid]::NewGuid().ToString('N'))
        $expandedSlots = @(Expand-WtbTexture $candidate.File.FullName $temporaryDirectory)
        $slotRecords = [Collections.Generic.List[object]]::new()
        foreach ($slot in $expandedSlots) {
            $inputRelative = ConvertTo-WorkspacePath (Join-Path (Join-Path 'unpack\data' $relativeDirectory) ([IO.Path]::GetFileName($slot.Path)))
            $inputTarget = Resolve-WorkspaceFile $workspaceRoot $inputRelative
            $inputDirectory = [IO.Path]::GetDirectoryName($inputTarget)
            New-Item -ItemType Directory -Force -Path $inputDirectory | Out-Null
            if (Test-Path -LiteralPath $inputTarget -PathType Leaf) {
                $preservedDds++
            } else {
                [IO.File]::Copy($slot.Path, $inputTarget, $false)
            }
            $slotRecords.Add([PSCustomObject]@{
                Index = $slot.Index
                Path = $inputRelative
                BaselineSha256 = Get-WorkspaceSha256 $inputTarget
            })
        }

        $newRecords.Add([PSCustomObject]@{
            Source = $candidate.Source
            Output = ConvertTo-WorkspacePath (Join-Path 'build\data' $candidate.RelativeDataPath)
            SourceSha256 = Get-WorkspaceSha256 $sourceTarget
            Slots = @($slotRecords)
            Category = Get-CategoryLabel $labels $candidate.CategoryKey
        })
    }

    $workspace.UIImages = @($existingRecords + @($newRecords) | Sort-Object Source)
    $json = $workspace | ConvertTo-Json -Depth 10
    $temporaryJson = Join-Path $backupDir "workspace.$stamp.tmp"
    [IO.File]::WriteAllText($temporaryJson, $json, [Text.UTF8Encoding]::new($false))
    [IO.File]::Replace($temporaryJson, $workspaceJson, $backupPath, $true)

    $manifestPath = Join-Path $workspaceRoot 'manifest.md'
    if (Test-Path -LiteralPath $manifestPath -PathType Leaf) {
        $summary = "`n`n---`n`n## UI-image incremental backfill $stamp`n`n- Existing records preserved: $($existingRecords.Count)`n- New records added: $($newRecords.Count)`n- Existing source files preserved: $preservedSources`n- Existing DDS files preserved: $preservedDds`n- Workspace backup: ``$($backupPath.Substring($workspaceRoot.Length + 1).Replace('\','/'))```n"
        [IO.File]::AppendAllText($manifestPath, $summary, [Text.UTF8Encoding]::new($false))
    }
} finally {
    if (Test-Path -LiteralPath $temporaryRoot) {
        Remove-Item -LiteralPath $temporaryRoot -Recurse -Force
    }
}

Write-Host "AddedUIImages=$($newRecords.Count)"
Write-Host "PreservedSourceFiles=$preservedSources"
Write-Host "PreservedDdsFiles=$preservedDds"
Write-Host "Backup=$backupPath"
