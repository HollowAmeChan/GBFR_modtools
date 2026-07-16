param(
    [string]$ManifestPath = "",
    [switch]$BuildChanged,
    [switch]$RestoreChanged,
    [switch]$UiSmokeTest
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$libRoot = Join-Path $PSScriptRoot "_lib"
$flatcExe = Join-Path $libRoot "flatc.exe"
$graniteExe = Join-Path $libRoot "GraniteTextureReader.exe"
$texconvExe = Join-Path $libRoot "texconv.exe"
$nierCliExe = Get-ChildItem -LiteralPath $libRoot -Directory -Filter "nier_cli_mgrr_*" -ErrorAction SilentlyContinue |
    Sort-Object Name -Descending |
    ForEach-Object { Join-Path $_.FullName "nier_cli_mgrr.exe" } |
    Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
    Select-Object -First 1
$schemaFbs = Join-Path $libRoot "MMat_ModelMaterial.fbs"
$B = ConvertFrom-Json ([IO.File]::ReadAllText((Join-Path $libRoot "builder_strings_zh.json"), [Text.Encoding]::UTF8))
. (Join-Path $libRoot "workspace_lib.ps1")

function Get-WorkspaceOperations([string]$Manifest) {
    if (-not (Test-Path -LiteralPath $Manifest -PathType Leaf)) {
        throw "$($B.err_manifest_missing): $Manifest"
    }
    if ([IO.Path]::GetFileName($Manifest) -ine "manifest.md") {
        throw $B.err_select_manifest
    }

    $root = [IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($Manifest))
    $workspacePath = Join-Path $root "workspace.json"
    if (-not (Test-Path -LiteralPath $workspacePath -PathType Leaf)) {
        throw $B.err_workspace_json
    }
    $workspace = ConvertFrom-Json ([IO.File]::ReadAllText($workspacePath, [Text.Encoding]::UTF8))
    if ([int]$workspace.Version -ne 1) { throw "$($B.err_version): $($workspace.Version)" }

    $operations = [System.Collections.Generic.List[PSCustomObject]]::new()
    $missing = 0
    foreach ($texture in @($workspace.Textures)) {
        $existingSlots = [System.Collections.Generic.List[PSCustomObject]]::new()
        $changed = $false
        foreach ($slot in @($texture.Slots)) {
            $slotPath = Resolve-WorkspaceFile $root ([string]$slot.Path)
            if (-not (Test-Path -LiteralPath $slotPath -PathType Leaf)) { continue }
            $currentHash = Get-WorkspaceSha256 $slotPath
            if ($currentHash -ne [string]$slot.BaselineSha256) { $changed = $true }
            $existingSlots.Add([PSCustomObject]@{ Index = [int]$slot.Index; Path = $slotPath })
        }
        $available = $existingSlots.Count -gt 0
        if (-not $available) { $missing++ }
        $inputNames = if ($available) {
            @($existingSlots | ForEach-Object { [IO.Path]::GetFileName($_.Path) })
        } else {
            @($texture.Slots | ForEach-Object { [IO.Path]::GetFileName([string]$_.Path) })
        }

        $operations.Add([PSCustomObject]@{
            Kind = "texture"
            TypeLabel = $B.type_texture
            InputLabel = ($inputNames -join ", ")
            OutputLabel = [string]$texture.Output
            Changed = $changed
            Available = $available
            Record = $texture
            Slots = @($existingSlots)
        })
    }

    foreach ($material in @($workspace.Materials)) {
        $jsonPath = Resolve-WorkspaceFile $root ([string]$material.Json)
        $available = Test-Path -LiteralPath $jsonPath -PathType Leaf
        if (-not $available) { $missing++ }
        $changed = $available -and (Get-WorkspaceSha256 $jsonPath) -ne [string]$material.BaselineSha256
        $operations.Add([PSCustomObject]@{
            Kind = "mmat"
            TypeLabel = $B.type_mmat
            InputLabel = [IO.Path]::GetFileName($jsonPath)
            OutputLabel = [string]$material.Output
            Changed = $changed
            Available = $available
            Record = $material
            JsonPath = $jsonPath
        })
    }

    if ($workspace.PSObject.Properties.Name -contains "NewTextures") {
        foreach ($texture in @($workspace.NewTextures)) {
            $inputPath = Resolve-WorkspaceFile $root ([string]$texture.Input)
            $available = Test-Path -LiteralPath $inputPath -PathType Leaf
            if (-not $available) { $missing++ }
            $changed = $available -and (Get-WorkspaceSha256 $inputPath) -ne [string]$texture.BaselineSha256
            $operations.Add([PSCustomObject]@{
                Kind = "new_texture"
                TypeLabel = $B.type_new_texture
                InputLabel = [IO.Path]::GetFileName($inputPath)
                OutputLabel = [string]$texture.Output
                Changed = $changed
                Available = $available
                Record = $texture
                DdsPath = $inputPath
            })
        }
    }

    return [PSCustomObject]@{
        Root = $root
        Workspace = $workspace
        Operations = @($operations)
        MissingCount = $missing
    }
}

function Invoke-WorkspaceBuild([object]$Context, [object[]]$Operations, [scriptblock]$Logger = $null) {
    $ok = 0
    $failed = 0
    foreach ($operation in @($Operations)) {
        try {
            if ($operation.PSObject.Properties.Name -contains "Available" -and -not $operation.Available) {
                throw $B.err_missing_input_build
            }
            $outputPath = Resolve-WorkspaceFile $Context.Root ([string]$operation.Record.Output)
            if ($operation.Kind -eq "texture") {
                $sourcePath = Resolve-WorkspaceFile $Context.Root ([string]$operation.Record.Source)
                if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
                    throw "$($B.err_source_missing): $sourcePath"
                }
                if ((Get-WorkspaceSha256 $sourcePath) -ne [string]$operation.Record.SourceSha256) {
                    throw "$($B.err_source_changed): $sourcePath"
                }
                $slots = @{}
                foreach ($slot in @($operation.Slots)) { $slots[[int]$slot.Index] = [string]$slot.Path }
                New-WtbTexture $sourcePath $slots $outputPath | Out-Null
            } elseif ($operation.Kind -eq "new_texture") {
                if (-not $nierCliExe) { throw $B.err_nier_cli_missing }
                New-WtbTextureFromDds $nierCliExe ([string]$operation.DdsPath) $outputPath ([uint32]$operation.Record.TextureId) | Out-Null
            } elseif ($operation.Kind -eq "mmat") {
                if (-not (Test-Path -LiteralPath $flatcExe)) { throw $B.err_flatc_missing }
                if (-not (Test-Path -LiteralPath $schemaFbs)) { throw $B.err_schema_missing }
                Convert-JsonToMmat $flatcExe $schemaFbs ([string]$operation.JsonPath) $outputPath | Out-Null
            } else {
                throw "$($B.err_unknown_type): $($operation.Kind)"
            }
            $ok++
            if ($Logger) { & $Logger "[OK] $($operation.TypeLabel) -> $($operation.OutputLabel)" }
        } catch {
            $failed++
            if ($Logger) { & $Logger "[$($B.failed)] $($operation.OutputLabel): $($_.Exception.Message)" }
        }
    }
    return [PSCustomObject]@{ Success = $ok; Failed = $failed }
}

function Restore-WorkspaceTexture([object]$Context, [object]$Operation) {
    $sourcePath = Resolve-WorkspaceFile $Context.Root ([string]$Operation.Record.Source)
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
        throw "$($B.err_source_missing): $sourcePath"
    }
    if ((Get-WorkspaceSha256 $sourcePath) -ne [string]$Operation.Record.SourceSha256) {
        throw "$($B.err_source_changed): $sourcePath"
    }

    $tempDir = Join-Path ([IO.Path]::GetTempPath()) ("gbfr_restore_wtb_" + [Guid]::NewGuid().ToString('N'))
    try {
        $restoredSlots = @(Expand-WtbTexture $sourcePath $tempDir)
        $restorePlan = [System.Collections.Generic.List[PSCustomObject]]::new()
        foreach ($slot in @($Operation.Record.Slots)) {
            $restored = $restoredSlots | Where-Object { [int]$_.Index -eq [int]$slot.Index } | Select-Object -First 1
            if ($null -eq $restored) { throw "Source texture is missing WTB slot $($slot.Index)" }
            if ([string]$restored.Sha256 -ne [string]$slot.BaselineSha256) {
                throw "Restored DDS does not match workspace baseline: $($slot.Path)"
            }
            $restorePlan.Add([PSCustomObject]@{
                Source = [string]$restored.Path
                Destination = Resolve-WorkspaceFile $Context.Root ([string]$slot.Path)
            })
        }
        foreach ($item in $restorePlan) {
            $destination = [string]$item.Destination
            $destinationDir = [IO.Path]::GetDirectoryName($destination)
            New-Item -ItemType Directory -Force -Path $destinationDir | Out-Null
            [IO.File]::Copy([string]$item.Source, $destination, $true)
        }
    } finally {
        if (Test-Path -LiteralPath $tempDir) { Remove-Item -LiteralPath $tempDir -Recurse -Force }
    }
}

function Restore-WorkspaceMaterial([object]$Context, [object]$Operation) {
    if (-not (Test-Path -LiteralPath $flatcExe -PathType Leaf)) { throw $B.err_flatc_missing }
    $sourcePath = Resolve-WorkspaceFile $Context.Root ([string]$Operation.Record.Source)
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
        throw "$($B.err_source_missing): $sourcePath"
    }
    if ($Operation.Record.PSObject.Properties.Name -contains "SourceSha256" -and
        (Get-WorkspaceSha256 $sourcePath) -ne [string]$Operation.Record.SourceSha256) {
        throw "$($B.err_source_changed): $sourcePath"
    }
    $jsonPath = Resolve-WorkspaceFile $Context.Root ([string]$Operation.Record.Json)
    $tempDir = Join-Path ([IO.Path]::GetTempPath()) ("gbfr_restore_mmat_" + [Guid]::NewGuid().ToString('N'))
    $tempJson = Join-Path $tempDir ([IO.Path]::GetFileName($jsonPath))
    try {
        New-Item -ItemType Directory -Force -Path $tempDir | Out-Null
        Convert-MmatToJson $flatcExe $schemaFbs $sourcePath $tempJson | Out-Null
        if ((Get-WorkspaceSha256 $tempJson) -ne [string]$Operation.Record.BaselineSha256) {
            throw "Restored mmat JSON does not match workspace baseline: $($Operation.Record.Json)"
        }
        $destinationDir = [IO.Path]::GetDirectoryName($jsonPath)
        New-Item -ItemType Directory -Force -Path $destinationDir | Out-Null
        [IO.File]::Copy($tempJson, $jsonPath, $true)
    } finally {
        if (Test-Path -LiteralPath $tempDir) { Remove-Item -LiteralPath $tempDir -Recurse -Force }
    }
}

function Restore-WorkspaceGraniteTexture([object]$Context, [object]$Operation) {
    if (-not (Test-Path -LiteralPath $graniteExe -PathType Leaf)) { throw $B.err_granite_missing }
    if (-not (Test-Path -LiteralPath $texconvExe -PathType Leaf)) { throw $B.err_texconv_missing }
    if (-not ($Context.Workspace.PSObject.Properties.Name -contains "GameDataRoot")) {
        throw $B.err_game_data_root
    }

    $dataRoot = [IO.Path]::GetFullPath([string]$Context.Workspace.GameDataRoot)
    $gtsPath = [IO.Path]::GetFullPath((Join-Path $dataRoot (([string]$Operation.Record.GraniteGts).Replace('/', '\'))))
    $dataPrefix = $dataRoot.TrimEnd([char[]]@('\','/')) + [IO.Path]::DirectorySeparatorChar
    if (-not $gtsPath.StartsWith($dataPrefix, [StringComparison]::OrdinalIgnoreCase) -or
        -not (Test-Path -LiteralPath $gtsPath -PathType Leaf)) {
        throw "$($B.err_granite_source): $gtsPath"
    }

    $ddsPath = Resolve-WorkspaceFile $Context.Root ([string]$Operation.Record.Input)
    $baseName = [IO.Path]::GetFileNameWithoutExtension($ddsPath)
    $slot = [regex]::Match($baseName, "_(albd|msk1|msk2|nrml)$").Groups[1].Value
    if (-not $slot) { throw "Unsupported Granite texture slot: $baseName" }
    $format = switch ($slot) {
        "albd" { "BC7_UNORM_SRGB" }
        "nrml" { "BC5_UNORM" }
        default { "BC7_UNORM" }
    }

    $tempRoot = Join-Path ([IO.Path]::GetTempPath()) ("gbfr_restore_granite_" + [Guid]::NewGuid().ToString('N'))
    $tgaDir = Join-Path $tempRoot "tga"
    $ddsDir = Join-Path $tempRoot "dds"
    try {
        New-Item -ItemType Directory -Force -Path $tgaDir,$ddsDir | Out-Null
        $extractOutput = @(& $graniteExe extract -t $gtsPath -f ([string]$Operation.Record.GraniteHash) -o $tgaDir -l -1 2>&1)
        $tgaPath = Join-Path $tgaDir "$baseName.tga"
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $tgaPath -PathType Leaf)) {
            throw "Granite restore failed for $baseName`n$($extractOutput -join [Environment]::NewLine)"
        }

        $convertOutput = @(& $texconvExe -nologo -y -m 0 -dx10 -f $format -o $ddsDir $tgaPath 2>&1)
        $restoredDds = Join-Path $ddsDir "$baseName.dds"
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $restoredDds -PathType Leaf)) {
            throw "texconv restore failed for $baseName`n$($convertOutput -join [Environment]::NewLine)"
        }
        if ((Get-WorkspaceSha256 $restoredDds) -ne [string]$Operation.Record.BaselineSha256) {
            throw "Restored Granite DDS does not match workspace baseline: $($Operation.Record.Input)"
        }

        $destinationDir = [IO.Path]::GetDirectoryName($ddsPath)
        New-Item -ItemType Directory -Force -Path $destinationDir | Out-Null
        [IO.File]::Copy($restoredDds, $ddsPath, $true)
    } finally {
        if (Test-Path -LiteralPath $tempRoot) { Remove-Item -LiteralPath $tempRoot -Recurse -Force }
    }
}

function Get-WorkspaceMaterialA4Count([object]$Context, [object]$Operation) {
    if ([string]$Operation.Kind -ne "mmat" -or -not $Operation.Available) { return 0 }
    $jsonPath = Resolve-WorkspaceFile $Context.Root ([string]$Operation.Record.Json)
    $material = ConvertFrom-Json ([IO.File]::ReadAllText($jsonPath, [Text.Encoding]::UTF8))
    return @($material.Entries1 | Where-Object {
        $null -ne $_ -and $_.PSObject.Properties.Name -contains "A4" -and $null -ne $_.A4
    }).Count
}

function Remove-WorkspaceMaterialA4([object]$Context, [object]$Operation) {
    if ([string]$Operation.Kind -ne "mmat") { throw $B.err_a4_mmat_only }
    if (-not $Operation.Available) { throw $B.err_missing_input_build }
    $jsonPath = Resolve-WorkspaceFile $Context.Root ([string]$Operation.Record.Json)
    $material = ConvertFrom-Json ([IO.File]::ReadAllText($jsonPath, [Text.Encoding]::UTF8))
    $removed = 0
    foreach ($entry in @($material.Entries1)) {
        if ($null -ne $entry -and
            $entry.PSObject.Properties.Name -contains "A4" -and
            $null -ne $entry.A4) {
            $entry.PSObject.Properties.Remove("A4")
            $removed++
        }
    }
    if ($removed -eq 0) { return 0 }

    $tempPath = "$jsonPath.tmp"
    try {
        $json = $material | ConvertTo-Json -Depth 100
        [IO.File]::WriteAllText($tempPath, $json, [Text.UTF8Encoding]::new($false))
        ConvertFrom-Json ([IO.File]::ReadAllText($tempPath, [Text.Encoding]::UTF8)) | Out-Null
        Move-Item -LiteralPath $tempPath -Destination $jsonPath -Force
    } finally {
        if (Test-Path -LiteralPath $tempPath) { Remove-Item -LiteralPath $tempPath -Force }
    }
    return $removed
}

function Restore-WorkspaceOperations([object]$Context, [object[]]$Operations, [scriptblock]$Logger = $null) {
    $ok = 0
    $failed = 0
    foreach ($operation in @($Operations)) {
        try {
            switch ([string]$operation.Kind) {
                "texture" { Restore-WorkspaceTexture $Context $operation }
                "new_texture" { Restore-WorkspaceGraniteTexture $Context $operation }
                "mmat" { Restore-WorkspaceMaterial $Context $operation }
                default { throw "$($B.err_unknown_type): $($operation.Kind)" }
            }
            $ok++
            if ($Logger) { & $Logger "[OK] $($B.restored) -> $($operation.InputLabel)" }
        } catch {
            $failed++
            if ($Logger) { & $Logger "[$($B.failed)] $($operation.InputLabel): $($_.Exception.Message)" }
        }
    }
    return [PSCustomObject]@{ Success = $ok; Failed = $failed }
}

if ($BuildChanged) {
    if (-not $ManifestPath) { throw $B.err_build_changed_manifest }
    $context = Get-WorkspaceOperations $ManifestPath
    $selected = @($context.Operations | Where-Object { $_.Changed })
    $result = Invoke-WorkspaceBuild $context $selected { param($message) Write-Host $message }
    Write-Host "$($B.build_finished): $($B.success) $($result.Success), $($B.failed) $($result.Failed)"
    if ($result.Failed -gt 0) { exit 1 }
    exit 0
}

if ($RestoreChanged) {
    if (-not $ManifestPath) { throw $B.err_build_changed_manifest }
    $context = Get-WorkspaceOperations $ManifestPath
    $selected = @($context.Operations | Where-Object { $_.Changed -or -not $_.Available })
    $result = Restore-WorkspaceOperations $context $selected { param($message) Write-Host $message }
    Write-Host "$($B.restore_finished): $($B.success) $($result.Success), $($B.failed) $($result.Failed)"
    if ($result.Failed -gt 0) { exit 1 }
    exit 0
}

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[Windows.Forms.Application]::EnableVisualStyles()

$form = New-Object Windows.Forms.Form
$form.Text = $B.app_title
$form.ClientSize = New-Object Drawing.Size(980, 650)
$form.MinimumSize = New-Object Drawing.Size(780, 520)
$form.StartPosition = "CenterScreen"
$form.Font = New-Object Drawing.Font("Microsoft YaHei UI", 9)
$form.AllowDrop = $true

$lblManifest = New-Object Windows.Forms.Label
$lblManifest.Text = $B.workspace_manifest
$lblManifest.Location = New-Object Drawing.Point(14, 17)
$lblManifest.AutoSize = $true
$form.Controls.Add($lblManifest)

$txtManifest = New-Object Windows.Forms.TextBox
$txtManifest.Location = New-Object Drawing.Point(130, 13)
$txtManifest.Size = New-Object Drawing.Size(720, 27)
$txtManifest.Anchor = "Top,Left,Right"
$txtManifest.ReadOnly = $true
$form.Controls.Add($txtManifest)

$btnBrowse = New-Object Windows.Forms.Button
$btnBrowse.Text = $B.browse
$btnBrowse.Location = New-Object Drawing.Point(862, 11)
$btnBrowse.Size = New-Object Drawing.Size(100, 30)
$btnBrowse.Anchor = "Top,Right"
$form.Controls.Add($btnBrowse)

$lblSummary = New-Object Windows.Forms.Label
$lblSummary.Text = $B.select_hint
$lblSummary.Location = New-Object Drawing.Point(14, 54)
$lblSummary.Size = New-Object Drawing.Size(948, 24)
$lblSummary.Anchor = "Top,Left,Right"
$form.Controls.Add($lblSummary)

$grid = New-Object Windows.Forms.DataGridView
$grid.Location = New-Object Drawing.Point(14, 82)
$grid.Size = New-Object Drawing.Size(948, 390)
$grid.Anchor = "Top,Bottom,Left,Right"
$grid.AllowUserToAddRows = $false
$grid.AllowUserToDeleteRows = $false
$grid.AllowUserToResizeRows = $false
$grid.AutoGenerateColumns = $false
$grid.MultiSelect = $true
$grid.RowHeadersVisible = $false
$grid.SelectionMode = "FullRowSelect"
$grid.EditMode = "EditOnEnter"
$grid.BackgroundColor = [Drawing.SystemColors]::Window
$grid.BorderStyle = "Fixed3D"
$grid.RowTemplate.Height = 28
$grid.ColumnHeadersHeight = 32

$colSelected = New-Object Windows.Forms.DataGridViewCheckBoxColumn
$colSelected.Name = "Selected"
$colSelected.HeaderText = $B.col_select
$colSelected.Width = 50
$colSelected.SortMode = "NotSortable"
[void]$grid.Columns.Add($colSelected)

foreach ($definition in @(
    @{ Name = "State"; Header = $B.col_state; Width = 75 },
    @{ Name = "Operation"; Header = $B.col_operation; Width = 90 },
    @{ Name = "Input"; Header = $B.col_input; Width = 230 }
)) {
    $column = New-Object Windows.Forms.DataGridViewTextBoxColumn
    $column.Name = $definition.Name
    $column.HeaderText = $definition.Header
    $column.Width = $definition.Width
    $column.ReadOnly = $true
    $column.SortMode = "NotSortable"
    [void]$grid.Columns.Add($column)
}

$colOutput = New-Object Windows.Forms.DataGridViewTextBoxColumn
$colOutput.Name = "Output"
$colOutput.HeaderText = $B.col_output
$colOutput.MinimumWidth = 260
$colOutput.AutoSizeMode = "Fill"
$colOutput.ReadOnly = $true
$colOutput.SortMode = "NotSortable"
[void]$grid.Columns.Add($colOutput)

$colRestore = New-Object Windows.Forms.DataGridViewButtonColumn
$colRestore.Name = "Restore"
$colRestore.HeaderText = $B.col_restore
$colRestore.Width = 70
$colRestore.ReadOnly = $true
$colRestore.FlatStyle = "Flat"
$colRestore.SortMode = "NotSortable"
[void]$grid.Columns.Add($colRestore)

$colA4 = New-Object Windows.Forms.DataGridViewButtonColumn
$colA4.Name = "A4Action"
$colA4.HeaderText = $B.col_mmat_action
$colA4.Width = 110
$colA4.ReadOnly = $true
$colA4.FlatStyle = "Flat"
$colA4.SortMode = "NotSortable"
[void]$grid.Columns.Add($colA4)
$form.Controls.Add($grid)

$btnModified = New-Object Windows.Forms.Button
$btnModified.Text = $B.select_modified
$btnModified.Location = New-Object Drawing.Point(14, 484)
$btnModified.Size = New-Object Drawing.Size(115, 32)
$btnModified.Anchor = "Bottom,Left"
$form.Controls.Add($btnModified)

$btnClear = New-Object Windows.Forms.Button
$btnClear.Text = $B.clear_selection
$btnClear.Location = New-Object Drawing.Point(138, 484)
$btnClear.Size = New-Object Drawing.Size(100, 32)
$btnClear.Anchor = "Bottom,Left"
$form.Controls.Add($btnClear)

$btnRefresh = New-Object Windows.Forms.Button
$btnRefresh.Text = $B.refresh_list
$btnRefresh.Location = New-Object Drawing.Point(247, 484)
$btnRefresh.Size = New-Object Drawing.Size(100, 32)
$btnRefresh.Anchor = "Bottom,Left"
$form.Controls.Add($btnRefresh)

$btnRestore = New-Object Windows.Forms.Button
$btnRestore.Text = $B.restore_selected
$btnRestore.Location = New-Object Drawing.Point(356, 484)
$btnRestore.Size = New-Object Drawing.Size(130, 32)
$btnRestore.Anchor = "Bottom,Left"
$btnRestore.Enabled = $false
$form.Controls.Add($btnRestore)

$btnOpenBuild = New-Object Windows.Forms.Button
$btnOpenBuild.Text = $B.open_build
$btnOpenBuild.Location = New-Object Drawing.Point(495, 484)
$btnOpenBuild.Size = New-Object Drawing.Size(105, 32)
$btnOpenBuild.Anchor = "Bottom,Left"
$form.Controls.Add($btnOpenBuild)

$btnBuild = New-Object Windows.Forms.Button
$btnBuild.Text = $B.build_selected
$btnBuild.Location = New-Object Drawing.Point(812, 482)
$btnBuild.Size = New-Object Drawing.Size(150, 36)
$btnBuild.Anchor = "Bottom,Right"
$btnBuild.Enabled = $false
$form.Controls.Add($btnBuild)

$log = New-Object Windows.Forms.TextBox
$log.Location = New-Object Drawing.Point(14, 528)
$log.Size = New-Object Drawing.Size(948, 105)
$log.Anchor = "Bottom,Left,Right"
$log.Multiline = $true
$log.ScrollBars = "Vertical"
$log.ReadOnly = $true
$log.Font = New-Object Drawing.Font("Consolas", 9)
$form.Controls.Add($log)

$script:context = $null
function Add-Log([string]$Message) {
    $log.AppendText($Message + "`r`n")
    $log.SelectionStart = $log.TextLength
    $log.ScrollToCaret()
}

function Update-SelectionSummary {
    if ($null -eq $script:context) {
        $btnBuild.Enabled = $false
        $btnRestore.Enabled = $false
        return
    }
    $checked = @(Get-CheckedGridOperations).Count
    $btnBuild.Enabled = $checked -gt 0
    $btnRestore.Enabled = $checked -gt 0
    $changed = @($script:context.Operations | Where-Object { $_.Changed }).Count
    $lblSummary.Text = "$($script:context.Workspace.CharacterId) | $($B.candidate) $($script:context.Operations.Count) | $($B.modified_count) $changed | $($B.selected_count) $checked | $($B.missing_count) $($script:context.MissingCount)"
}

function Get-OperationKey([object]$Operation) {
    return "$($Operation.Kind)|$($Operation.OutputLabel)"
}

function Get-CheckedGridOperations {
    return @($grid.Rows | Where-Object {
        $null -ne $_.Tag -and [bool]$_.Cells["Selected"].Value
    } | ForEach-Object { $_.Tag })
}

function Load-Manifest([string]$Path, [switch]$PreserveSelection) {
    try {
        $checkedKeys = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        $knownKeys = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        if ($PreserveSelection) {
            foreach ($existingRow in @($grid.Rows)) {
                if ($null -eq $existingRow.Tag) { continue }
                $key = Get-OperationKey $existingRow.Tag
                $knownKeys.Add($key) | Out-Null
                if ([bool]$existingRow.Cells["Selected"].Value) { $checkedKeys.Add($key) | Out-Null }
            }
        }

        $script:context = Get-WorkspaceOperations $Path
        $txtManifest.Text = [IO.Path]::GetFullPath($Path)
        $grid.SuspendLayout()
        $grid.Rows.Clear()
        foreach ($operation in @($script:context.Operations)) {
            $state = if (-not $operation.Available) { $B.state_missing }
                     elseif ($operation.Changed) { $B.state_modified }
                     else { $B.state_unchanged }
            $key = Get-OperationKey $operation
            $isChecked = if ($PreserveSelection -and $knownKeys.Contains($key)) {
                $checkedKeys.Contains($key)
            } else {
                [bool]$operation.Changed
            }

            $a4Label = ""
            $a4ActionEnabled = $false
            if ([string]$operation.Kind -eq "mmat" -and $operation.Available) {
                try {
                    $a4Count = Get-WorkspaceMaterialA4Count $script:context $operation
                    if ($a4Count -gt 0) {
                        $a4Label = "$($B.clear_a4) ($a4Count)"
                        $a4ActionEnabled = $true
                    } else {
                        $a4Label = $B.a4_cleared
                    }
                } catch {
                    $a4Label = $B.a4_json_error
                }
            }

            $rowIndex = $grid.Rows.Add(
                $isChecked, $state, $operation.TypeLabel, $operation.InputLabel,
                $operation.OutputLabel, $B.restore_row, $a4Label
            )
            $row = $grid.Rows[$rowIndex]
            $row.Tag = $operation
            $row.Cells["Input"].ToolTipText = $operation.InputLabel
            $row.Cells["Output"].ToolTipText = $operation.OutputLabel
            $row.Cells["Restore"].Style.ForeColor = [Drawing.SystemColors]::ControlText
            if (-not $a4ActionEnabled) {
                $textCell = New-Object Windows.Forms.DataGridViewTextBoxCell
                $textCell.Value = $a4Label
                $row.Cells["A4Action"] = $textCell
                $row.Cells["A4Action"].ReadOnly = $true
            } else {
                $row.Cells["A4Action"].Style.ForeColor = [Drawing.SystemColors]::ControlText
            }
            if (-not $operation.Available) { $row.DefaultCellStyle.ForeColor = [Drawing.Color]::DarkOrange }
            elseif (-not $operation.Changed) { $row.DefaultCellStyle.ForeColor = [Drawing.Color]::Gray }
        }
        $grid.ResumeLayout()
        if ($PreserveSelection) { Add-Log $B.workspace_refreshed }
        else { Add-Log "$($B.workspace_loaded): $($script:context.Root)" }
        Update-SelectionSummary
    } catch {
        try { $grid.ResumeLayout() } catch {}
        if ($UiSmokeTest) { throw }
        [Windows.Forms.MessageBox]::Show($_.Exception.Message, $B.load_failed, "OK", "Error") | Out-Null
    }
}

$btnBrowse.Add_Click({
    $dialog = New-Object Windows.Forms.OpenFileDialog
    $dialog.Title = $B.select_dialog
    $dialog.Filter = "GBFR workspace manifest (manifest.md)|manifest.md|Markdown (*.md)|*.md"
    if ($dialog.ShowDialog() -eq "OK") { Load-Manifest $dialog.FileName }
})

$dropHandler = {
    if ($_.Data.GetDataPresent([Windows.Forms.DataFormats]::FileDrop)) {
        $files = @($_.Data.GetData([Windows.Forms.DataFormats]::FileDrop))
        if ($files.Count -gt 0) { Load-Manifest $files[0] }
    }
}.GetNewClosure()
$form.Add_DragEnter({ if ($_.Data.GetDataPresent([Windows.Forms.DataFormats]::FileDrop)) { $_.Effect = "Copy" } })
$form.Add_DragDrop($dropHandler)
$txtManifest.AllowDrop = $true
$txtManifest.Add_DragEnter({ if ($_.Data.GetDataPresent([Windows.Forms.DataFormats]::FileDrop)) { $_.Effect = "Copy" } })
$txtManifest.Add_DragDrop($dropHandler)

$grid.Add_CurrentCellDirtyStateChanged({
    if ($grid.IsCurrentCellDirty -and $grid.CurrentCell.ColumnIndex -eq $grid.Columns["Selected"].Index) {
        $grid.CommitEdit([Windows.Forms.DataGridViewDataErrorContexts]::Commit) | Out-Null
    }
})
$grid.Add_CellValueChanged({
    if ($_.RowIndex -ge 0 -and $_.ColumnIndex -eq $grid.Columns["Selected"].Index) {
        Update-SelectionSummary
    }
})
$btnModified.Add_Click({
    foreach ($row in $grid.Rows) {
        if ($null -ne $row.Tag) { $row.Cells["Selected"].Value = [bool]$row.Tag.Changed }
    }
    Update-SelectionSummary
})
$btnClear.Add_Click({
    foreach ($row in $grid.Rows) { $row.Cells["Selected"].Value = $false }
    Update-SelectionSummary
})
$btnRefresh.Add_Click({
    if ($txtManifest.Text) { Load-Manifest $txtManifest.Text -PreserveSelection }
})
$btnRestore.Add_Click({
    if ($null -eq $script:context) { return }
    $selected = @(Get-CheckedGridOperations)
    if ($selected.Count -eq 0) { return }
    $targets = ($selected | ForEach-Object { "- $($_.InputLabel)" }) -join "`r`n"
    $answer = [Windows.Forms.MessageBox]::Show(
        "$($B.confirm_restore_prefix) $($selected.Count) $($B.confirm_restore_suffix):`r`n`r`n$targets",
        $B.confirm_title, "OKCancel", "Warning"
    )
    if ($answer -ne "OK") { return }
    $btnRestore.Enabled = $false
    $result = Restore-WorkspaceOperations $script:context $selected { param($message) Add-Log $message }
    Add-Log "$($B.restore_finished): $($B.success) $($result.Success), $($B.failed) $($result.Failed)"
    Load-Manifest $txtManifest.Text
    if ($result.Failed -gt 0) {
        [Windows.Forms.MessageBox]::Show($B.partial_failure, $B.restore_finished, "OK", "Warning") | Out-Null
    }
})
$grid.Add_CellContentClick({
    if ($_.RowIndex -lt 0) { return }
    $row = $grid.Rows[$_.RowIndex]
    $operation = $row.Tag
    if ($null -eq $operation) { return }
    $columnName = $grid.Columns[$_.ColumnIndex].Name

    if ($columnName -eq "Restore") {
        $answer = [Windows.Forms.MessageBox]::Show(
            "$($B.confirm_restore_one):`r`n`r`n$($operation.InputLabel)",
            $B.confirm_title, "OKCancel", "Warning"
        )
        if ($answer -ne "OK") { return }
        $result = Restore-WorkspaceOperations $script:context @($operation) { param($message) Add-Log $message }
        Add-Log "$($B.restore_finished): $($B.success) $($result.Success), $($B.failed) $($result.Failed)"
        if ($result.Failed -eq 0) { $row.Cells["Selected"].Value = $false }
        Load-Manifest $txtManifest.Text -PreserveSelection
        if ($result.Failed -gt 0) {
            [Windows.Forms.MessageBox]::Show($B.partial_failure, $B.restore_finished, "OK", "Warning") | Out-Null
        }
        return
    }

    if ($columnName -eq "A4Action" -and [string]$operation.Kind -eq "mmat" -and $operation.Available) {
        try {
            $a4Count = Get-WorkspaceMaterialA4Count $script:context $operation
            if ($a4Count -le 0) { return }
            $answer = [Windows.Forms.MessageBox]::Show(
                "$($B.confirm_clear_a4_prefix) $a4Count $($B.confirm_clear_a4_suffix):`r`n`r`n$($operation.InputLabel)",
                $B.confirm_title, "OKCancel", "Warning"
            )
            if ($answer -ne "OK") { return }
            $removed = Remove-WorkspaceMaterialA4 $script:context $operation
            Add-Log "[OK] $($B.a4_removed): $($operation.InputLabel) ($removed)"
            $row.Cells["Selected"].Value = $true
            Load-Manifest $txtManifest.Text -PreserveSelection
        } catch {
            Add-Log "[$($B.failed)] $($operation.InputLabel): $($_.Exception.Message)"
            [Windows.Forms.MessageBox]::Show($_.Exception.Message, $B.a4_remove_failed, "OK", "Error") | Out-Null
        }
    }
})
$btnOpenBuild.Add_Click({
    if ($null -ne $script:context) {
        $path = Join-Path $script:context.Root "build"
        New-Item -ItemType Directory -Force -Path $path | Out-Null
        Start-Process explorer.exe -ArgumentList $path
    }
})
$btnBuild.Add_Click({
    $selected = @(Get-CheckedGridOperations)
    if ($selected.Count -eq 0) { return }
    $unavailable = @($selected | Where-Object { -not $_.Available })
    if ($unavailable.Count -gt 0) {
        [Windows.Forms.MessageBox]::Show($B.err_missing_input_build, $B.build_finished, "OK", "Warning") | Out-Null
        return
    }
    $targets = ($selected | ForEach-Object { "- $($_.OutputLabel)" }) -join "`r`n"
    $answer = [Windows.Forms.MessageBox]::Show(
        "$($B.confirm_prefix) $($selected.Count) $($B.confirm_suffix):`r`n`r`n$targets",
        $B.confirm_title, "OKCancel", "Information"
    )
    if ($answer -ne "OK") { return }
    $btnBuild.Enabled = $false
    $result = Invoke-WorkspaceBuild $script:context $selected { param($message) Add-Log $message }
    Add-Log "$($B.build_finished): $($B.success) $($result.Success), $($B.failed) $($result.Failed)"
    Load-Manifest $txtManifest.Text -PreserveSelection
    if ($result.Failed -gt 0) {
        [Windows.Forms.MessageBox]::Show($B.partial_failure, $B.build_finished, "OK", "Warning") | Out-Null
    }
})

if ($ManifestPath -and (Test-Path -LiteralPath $ManifestPath)) { Load-Manifest $ManifestPath }
if ($UiSmokeTest) {
    $mmatRows = @($grid.Rows | Where-Object { $null -ne $_.Tag -and [string]$_.Tag.Kind -eq "mmat" })
    $restoreButtons = @($grid.Rows | Where-Object { $_.Cells["Restore"].Value -eq $B.restore_row })
    $a4Buttons = @($mmatRows | Where-Object { [string]$_.Cells["A4Action"].Value -like "$($B.clear_a4)*" })
    Write-Host "UI smoke: rows=$($grid.Rows.Count), restore=$($restoreButtons.Count), bulkRestore=$($btnRestore.Text -eq $B.restore_selected), mmat=$($mmatRows.Count), clearA4=$($a4Buttons.Count)"
    exit 0
}
[void]$form.ShowDialog()
