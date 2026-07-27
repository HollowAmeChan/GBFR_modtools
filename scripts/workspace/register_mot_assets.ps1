param(
    [Parameter(Mandatory = $true)]
    [string]$WorkspacePath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "workspace_lib.ps1")

$workspaceFile = [IO.Path]::GetFullPath($WorkspacePath)
if (-not (Test-Path -LiteralPath $workspaceFile -PathType Leaf) -or
    [IO.Path]::GetFileName($workspaceFile) -ne "workspace.json") {
    throw "Select an existing workspace.json: $workspaceFile"
}

$workspaceRoot = [IO.Path]::GetDirectoryName($workspaceFile)
$document = ConvertFrom-Json ([IO.File]::ReadAllText($workspaceFile, [Text.Encoding]::UTF8))
if ([int]$document.Version -ne 1) { throw "Only workspace Version 1 is supported" }

$properties = @($document.PSObject.Properties.Name)
$sourceRootRelative = if ($properties -contains "SourceRoot") { [string]$document.SourceRoot } else { "source" }
$unpackRootRelative = if ($properties -contains "UnpackRoot") { [string]$document.UnpackRoot } else { "unpack" }
$buildRootRelative = if ($properties -contains "BuildRoot") { [string]$document.BuildRoot } else { "build" }
$sourceRoot = Resolve-WorkspaceFile $workspaceRoot $sourceRootRelative
$unpackRoot = Resolve-WorkspaceFile $workspaceRoot $unpackRootRelative

$modelIds = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($record in @($document.ModelFiles)) {
    if ([string]$record.FileType -ne "minfo") { continue }
    $value = if ($record.Input) { [string]$record.Input } else { [string]$record.Source }
    if ($value) { $modelIds.Add([IO.Path]::GetFileNameWithoutExtension($value)) | Out-Null }
}

$records = [System.Collections.Generic.List[PSCustomObject]]::new()
$existingByInput = @{}
if ($properties -contains "AnimationFiles") {
    foreach ($record in @($document.AnimationFiles)) {
        $records.Add($record)
        if ($record.Input) { $existingByInput[[string]$record.Input] = $true }
    }
}

$createdInputs = 0
$preservedInputs = 0
foreach ($modelId in ($modelIds | Sort-Object)) {
    $prefix = [regex]::Match($modelId, '^[a-z]+').Value
    if (-not $prefix) { continue }
    $sourceDirectory = Join-Path $sourceRoot "data\$prefix\$modelId"
    if (-not (Test-Path -LiteralPath $sourceDirectory -PathType Container)) { continue }
    foreach ($source in (Get-ChildItem -LiteralPath $sourceDirectory -Filter "*.mot" -File | Sort-Object Name)) {
        $sourceRelativeToRoot = $source.FullName.Substring($sourceRoot.Length).TrimStart([char[]]@('\', '/'))
        $sourceWorkspacePath = ConvertTo-WorkspacePath (Join-Path $sourceRootRelative $sourceRelativeToRoot)
        $inputWorkspacePath = ConvertTo-WorkspacePath (Join-Path $unpackRootRelative $sourceRelativeToRoot)
        $outputWorkspacePath = ConvertTo-WorkspacePath (Join-Path $buildRootRelative $sourceRelativeToRoot)
        $inputPath = Resolve-WorkspaceFile $workspaceRoot $inputWorkspacePath
        $sourceHash = Get-WorkspaceSha256 $source.FullName

        if (-not (Test-Path -LiteralPath $inputPath -PathType Leaf)) {
            [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($inputPath)) | Out-Null
            [IO.File]::Copy($source.FullName, $inputPath, $false)
            $createdInputs++
        } else {
            $preservedInputs++
        }

        if (-not $existingByInput.ContainsKey($inputWorkspacePath)) {
            $records.Add([PSCustomObject][ordered]@{
                ModelId = $modelId
                Source = $sourceWorkspacePath
                SourceSha256 = $sourceHash
                Input = $inputWorkspacePath
                Output = $outputWorkspacePath
                BaselineSha256 = $sourceHash
                FileType = "mot"
            })
            $existingByInput[$inputWorkspacePath] = $true
        }
    }
}

if ($properties -contains "AnimationFiles") {
    $document.AnimationFiles = @($records)
} else {
    $document | Add-Member -NotePropertyName AnimationFiles -NotePropertyValue @($records)
}

$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$backup = "$workspaceFile.pre-mot-$timestamp.bak"
$temporary = "$workspaceFile.mot.tmp"
$json = $document | ConvertTo-Json -Depth 16
[IO.File]::WriteAllText($temporary, $json, [Text.UTF8Encoding]::new($false))
[IO.File]::Replace($temporary, $workspaceFile, $backup, $true)

Write-Output ([PSCustomObject]@{
    Workspace = $workspaceFile
    AnimationRecords = $records.Count
    CreatedUnpackFiles = $createdInputs
    PreservedUnpackFiles = $preservedInputs
    Backup = $backup
})
