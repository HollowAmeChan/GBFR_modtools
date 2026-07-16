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
$gbfrDataToolsExe = Join-Path $libRoot "GBFRDataTools.exe"
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

    if ($workspace.PSObject.Properties.Name -contains "ClothFiles") {
        foreach ($cloth in @($workspace.ClothFiles)) {
            $xmlPath = Resolve-WorkspaceFile $root ([string]$cloth.Xml)
            $available = Test-Path -LiteralPath $xmlPath -PathType Leaf
            if (-not $available) { $missing++ }
            $changed = $available -and (Get-WorkspaceSha256 $xmlPath) -ne [string]$cloth.BaselineSha256
            $typeLabel = switch ([string]$cloth.Category) {
                "clp" { $B.type_cloth_clp }
                "clh" { $B.type_cloth_clh }
                "sequence" { $B.type_cloth_sequence }
                "reset" { $B.type_cloth_reset }
                default { $B.type_cloth }
            }
            $operations.Add([PSCustomObject]@{
                Kind = "cloth"
                TypeLabel = $typeLabel
                InputLabel = [IO.Path]::GetFileName($xmlPath)
                OutputLabel = [string]$cloth.Output
                Changed = $changed
                Available = $available
                Record = $cloth
                XmlPath = $xmlPath
                ClothCategory = [string]$cloth.Category
            })
        }
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
            } elseif ($operation.Kind -eq "cloth") {
                if (-not (Test-Path -LiteralPath $gbfrDataToolsExe)) { throw $B.err_gbfrdatatools_missing }
                $tempOutput = "$outputPath.tmp"
                try {
                    Convert-XmlToBxm $gbfrDataToolsExe ([string]$operation.XmlPath) $tempOutput | Out-Null
                    Move-Item -LiteralPath $tempOutput -Destination $outputPath -Force
                } finally {
                    if (Test-Path -LiteralPath $tempOutput) { Remove-Item -LiteralPath $tempOutput -Force }
                }
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

function Restore-WorkspaceCloth([object]$Context, [object]$Operation) {
    if (-not (Test-Path -LiteralPath $gbfrDataToolsExe -PathType Leaf)) { throw $B.err_gbfrdatatools_missing }
    $sourcePath = Resolve-WorkspaceFile $Context.Root ([string]$Operation.Record.Source)
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
        throw "$($B.err_source_missing): $sourcePath"
    }
    if ((Get-WorkspaceSha256 $sourcePath) -ne [string]$Operation.Record.SourceSha256) {
        throw "$($B.err_source_changed): $sourcePath"
    }

    $xmlPath = Resolve-WorkspaceFile $Context.Root ([string]$Operation.Record.Xml)
    $tempDir = Join-Path ([IO.Path]::GetTempPath()) ("gbfr_restore_cloth_" + [Guid]::NewGuid().ToString('N'))
    $tempXml = Join-Path $tempDir ([IO.Path]::GetFileName($xmlPath))
    try {
        New-Item -ItemType Directory -Force -Path $tempDir | Out-Null
        Convert-BxmToXml $gbfrDataToolsExe $sourcePath $tempXml | Out-Null
        if ((Get-WorkspaceSha256 $tempXml) -ne [string]$Operation.Record.BaselineSha256) {
            throw "Restored cloth XML does not match workspace baseline: $($Operation.Record.Xml)"
        }
        $destinationDir = [IO.Path]::GetDirectoryName($xmlPath)
        New-Item -ItemType Directory -Force -Path $destinationDir | Out-Null
        [IO.File]::Copy($tempXml, $xmlPath, $true)
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
                "cloth" { Restore-WorkspaceCloth $Context $operation }
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

$tabs = New-Object Windows.Forms.TabControl
$tabs.Location = New-Object Drawing.Point(8, 50)
$tabs.Size = New-Object Drawing.Size(964, 592)
$tabs.Anchor = "Top,Bottom,Left,Right"

$pageBuild = New-Object Windows.Forms.TabPage
$pageBuild.Text = $B.tab_build
$pageMmat = New-Object Windows.Forms.TabPage
$pageMmat.Text = $B.tab_mmat
$pageCloth = New-Object Windows.Forms.TabPage
$pageCloth.Text = $B.tab_cloth
[void]$tabs.TabPages.Add($pageBuild)
[void]$tabs.TabPages.Add($pageMmat)
[void]$tabs.TabPages.Add($pageCloth)
$form.Controls.Add($tabs)

$lblSummary = New-Object Windows.Forms.Label
$lblSummary.Text = $B.select_hint
$lblSummary.Location = New-Object Drawing.Point(8, 8)
$lblSummary.Size = New-Object Drawing.Size(932, 24)
$lblSummary.Anchor = "Top,Left,Right"
$pageBuild.Controls.Add($lblSummary)

$grid = New-Object Windows.Forms.DataGridView
$grid.Location = New-Object Drawing.Point(8, 36)
$grid.Size = New-Object Drawing.Size(932, 356)
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

$colEdit = New-Object Windows.Forms.DataGridViewButtonColumn
$colEdit.Name = "Edit"
$colEdit.HeaderText = $B.col_edit
$colEdit.Width = 70
$colEdit.ReadOnly = $true
$colEdit.FlatStyle = "Flat"
$colEdit.SortMode = "NotSortable"
[void]$grid.Columns.Add($colEdit)
$pageBuild.Controls.Add($grid)

$btnModified = New-Object Windows.Forms.Button
$btnModified.Text = $B.select_modified
$btnModified.Location = New-Object Drawing.Point(8, 404)
$btnModified.Size = New-Object Drawing.Size(115, 32)
$btnModified.Anchor = "Bottom,Left"
$pageBuild.Controls.Add($btnModified)

$btnClear = New-Object Windows.Forms.Button
$btnClear.Text = $B.clear_selection
$btnClear.Location = New-Object Drawing.Point(132, 404)
$btnClear.Size = New-Object Drawing.Size(100, 32)
$btnClear.Anchor = "Bottom,Left"
$pageBuild.Controls.Add($btnClear)

$btnRefresh = New-Object Windows.Forms.Button
$btnRefresh.Text = $B.refresh_list
$btnRefresh.Location = New-Object Drawing.Point(241, 404)
$btnRefresh.Size = New-Object Drawing.Size(100, 32)
$btnRefresh.Anchor = "Bottom,Left"
$pageBuild.Controls.Add($btnRefresh)

$btnRestore = New-Object Windows.Forms.Button
$btnRestore.Text = $B.restore_selected
$btnRestore.Location = New-Object Drawing.Point(350, 404)
$btnRestore.Size = New-Object Drawing.Size(130, 32)
$btnRestore.Anchor = "Bottom,Left"
$btnRestore.Enabled = $false
$pageBuild.Controls.Add($btnRestore)

$btnOpenBuild = New-Object Windows.Forms.Button
$btnOpenBuild.Text = $B.open_build
$btnOpenBuild.Location = New-Object Drawing.Point(489, 404)
$btnOpenBuild.Size = New-Object Drawing.Size(105, 32)
$btnOpenBuild.Anchor = "Bottom,Left"
$pageBuild.Controls.Add($btnOpenBuild)

$btnBuild = New-Object Windows.Forms.Button
$btnBuild.Text = $B.build_selected
$btnBuild.Location = New-Object Drawing.Point(790, 402)
$btnBuild.Size = New-Object Drawing.Size(150, 36)
$btnBuild.Anchor = "Bottom,Right"
$btnBuild.Enabled = $false
$pageBuild.Controls.Add($btnBuild)

$log = New-Object Windows.Forms.TextBox
$log.Location = New-Object Drawing.Point(8, 448)
$log.Size = New-Object Drawing.Size(932, 102)
$log.Anchor = "Bottom,Left,Right"
$log.Multiline = $true
$log.ScrollBars = "Vertical"
$log.ReadOnly = $true
$log.Font = New-Object Drawing.Font("Consolas", 9)
$pageBuild.Controls.Add($log)

$lblMmatObject = New-Object Windows.Forms.Label
$lblMmatObject.Text = $B.edit_object
$lblMmatObject.Location = New-Object Drawing.Point(12, 16)
$lblMmatObject.AutoSize = $true
$pageMmat.Controls.Add($lblMmatObject)

$txtMmatObject = New-Object Windows.Forms.TextBox
$txtMmatObject.Location = New-Object Drawing.Point(90, 12)
$txtMmatObject.Size = New-Object Drawing.Size(650, 27)
$txtMmatObject.Anchor = "Top,Left,Right"
$txtMmatObject.ReadOnly = $true
$txtMmatObject.Text = $B.no_edit_object
$pageMmat.Controls.Add($txtMmatObject)

$btnClearMmatA4 = New-Object Windows.Forms.Button
$btnClearMmatA4.Text = $B.clear_all_a4
$btnClearMmatA4.Location = New-Object Drawing.Point(754, 10)
$btnClearMmatA4.Size = New-Object Drawing.Size(180, 32)
$btnClearMmatA4.Anchor = "Top,Right"
$btnClearMmatA4.Enabled = $false
$pageMmat.Controls.Add($btnClearMmatA4)

$lblMmatSummary = New-Object Windows.Forms.Label
$lblMmatSummary.Text = $B.mmat_no_selection
$lblMmatSummary.Location = New-Object Drawing.Point(12, 50)
$lblMmatSummary.Size = New-Object Drawing.Size(922, 24)
$lblMmatSummary.Anchor = "Top,Left,Right"
$pageMmat.Controls.Add($lblMmatSummary)

$mmatGrid = New-Object Windows.Forms.DataGridView
$mmatGrid.Location = New-Object Drawing.Point(12, 78)
$mmatGrid.Size = New-Object Drawing.Size(922, 462)
$mmatGrid.Anchor = "Top,Bottom,Left,Right"
$mmatGrid.AllowUserToAddRows = $false
$mmatGrid.AllowUserToDeleteRows = $false
$mmatGrid.AllowUserToResizeRows = $false
$mmatGrid.AutoGenerateColumns = $false
$mmatGrid.MultiSelect = $false
$mmatGrid.RowHeadersVisible = $false
$mmatGrid.SelectionMode = "FullRowSelect"
$mmatGrid.ReadOnly = $true
$mmatGrid.BackgroundColor = [Drawing.SystemColors]::Window
$mmatGrid.RowTemplate.Height = 28
$mmatGrid.ColumnHeadersHeight = 32

foreach ($definition in @(
    @{ Name = "Entry"; Header = $B.col_entry; Width = 55 },
    @{ Name = "A5"; Header = "A5"; Width = 105 },
    @{ Name = "A1"; Header = "A1"; Width = 50 },
    @{ Name = "A2"; Header = "A2"; Width = 50 },
    @{ Name = "A3"; Header = "A3"; Width = 50 },
    @{ Name = "A4"; Header = "A4"; Width = 50 }
)) {
    $column = New-Object Windows.Forms.DataGridViewTextBoxColumn
    $column.Name = $definition.Name
    $column.HeaderText = $definition.Header
    $column.Width = $definition.Width
    $column.ReadOnly = $true
    $column.SortMode = "NotSortable"
    [void]$mmatGrid.Columns.Add($column)
}
$colMmatTextures = New-Object Windows.Forms.DataGridViewTextBoxColumn
$colMmatTextures.Name = "Textures"
$colMmatTextures.HeaderText = $B.col_texture_references
$colMmatTextures.AutoSizeMode = "Fill"
$colMmatTextures.MinimumWidth = 260
$colMmatTextures.ReadOnly = $true
$colMmatTextures.SortMode = "NotSortable"
[void]$mmatGrid.Columns.Add($colMmatTextures)
$pageMmat.Controls.Add($mmatGrid)

$lblClothObject = New-Object Windows.Forms.Label
$lblClothObject.Text = $B.edit_object
$lblClothObject.Location = New-Object Drawing.Point(12, 16)
$lblClothObject.AutoSize = $true
$pageCloth.Controls.Add($lblClothObject)

$txtClothObject = New-Object Windows.Forms.TextBox
$txtClothObject.Location = New-Object Drawing.Point(90, 12)
$txtClothObject.Size = New-Object Drawing.Size(730, 27)
$txtClothObject.Anchor = "Top,Left,Right"
$txtClothObject.ReadOnly = $true
$txtClothObject.Text = $B.no_edit_object
$pageCloth.Controls.Add($txtClothObject)

$btnChooseCloth = New-Object Windows.Forms.Button
$btnChooseCloth.Text = $B.browse
$btnChooseCloth.Location = New-Object Drawing.Point(834, 10)
$btnChooseCloth.Size = New-Object Drawing.Size(100, 32)
$btnChooseCloth.Anchor = "Top,Right"
$pageCloth.Controls.Add($btnChooseCloth)

$lblClothSummary = New-Object Windows.Forms.Label
$lblClothSummary.Text = $B.cloth_no_selection
$lblClothSummary.Location = New-Object Drawing.Point(12, 50)
$lblClothSummary.Size = New-Object Drawing.Size(922, 24)
$lblClothSummary.Anchor = "Top,Left,Right"
$pageCloth.Controls.Add($lblClothSummary)

$clothHeaderGrid = New-Object Windows.Forms.DataGridView
$clothHeaderGrid.Location = New-Object Drawing.Point(12, 78)
$clothHeaderGrid.Size = New-Object Drawing.Size(922, 130)
$clothHeaderGrid.AllowUserToAddRows = $false
$clothHeaderGrid.AllowUserToDeleteRows = $false
$clothHeaderGrid.AllowUserToResizeRows = $false
$clothHeaderGrid.AutoGenerateColumns = $false
$clothHeaderGrid.MultiSelect = $false
$clothHeaderGrid.RowHeadersVisible = $false
$clothHeaderGrid.ReadOnly = $true
$clothHeaderGrid.BackgroundColor = [Drawing.SystemColors]::Window
$clothHeaderGrid.RowTemplate.Height = 26
$clothHeaderGrid.ColumnHeadersHeight = 30
$colClothProperty = New-Object Windows.Forms.DataGridViewTextBoxColumn
$colClothProperty.Name = "Property"
$colClothProperty.HeaderText = $B.col_quick_property
$colClothProperty.Width = 210
$colClothProperty.ReadOnly = $true
$colClothProperty.SortMode = "NotSortable"
[void]$clothHeaderGrid.Columns.Add($colClothProperty)
$colClothValue = New-Object Windows.Forms.DataGridViewTextBoxColumn
$colClothValue.Name = "Value"
$colClothValue.HeaderText = $B.col_quick_value
$colClothValue.AutoSizeMode = "Fill"
$colClothValue.ReadOnly = $true
$colClothValue.SortMode = "NotSortable"
[void]$clothHeaderGrid.Columns.Add($colClothValue)
$pageCloth.Controls.Add($clothHeaderGrid)

$clothGrid = New-Object Windows.Forms.DataGridView
$clothGrid.Location = New-Object Drawing.Point(12, 216)
$clothGrid.Size = New-Object Drawing.Size(922, 324)
$clothGrid.Anchor = "Top,Bottom,Left,Right"
$clothGrid.AllowUserToAddRows = $false
$clothGrid.AllowUserToDeleteRows = $false
$clothGrid.AllowUserToResizeRows = $false
$clothGrid.AutoGenerateColumns = $false
$clothGrid.MultiSelect = $false
$clothGrid.RowHeadersVisible = $false
$clothGrid.ReadOnly = $true
$clothGrid.BackgroundColor = [Drawing.SystemColors]::Window
$clothGrid.RowTemplate.Height = 28
$clothGrid.ColumnHeadersHeight = 32
$pageCloth.Controls.Add($clothGrid)

foreach ($control in @(
    $lblSummary, $grid, $btnModified, $btnClear, $btnRefresh, $btnRestore,
    $btnOpenBuild, $btnBuild, $log, $lblMmatObject, $txtMmatObject,
    $btnClearMmatA4, $lblMmatSummary, $mmatGrid, $lblClothObject,
    $txtClothObject, $btnChooseCloth, $lblClothSummary, $clothHeaderGrid, $clothGrid
)) {
    $control.Anchor = "Top,Left"
}

function Update-WorkspaceEditorLayout {
    $buildWidth = $pageBuild.ClientSize.Width
    $buildHeight = $pageBuild.ClientSize.Height
    if ($buildWidth -gt 300 -and $buildHeight -gt 250) {
        $logHeight = 102
        $logY = $buildHeight - 8 - $logHeight
        $toolbarY = $logY - 44
        $gridHeight = [Math]::Max(100, $toolbarY - 12 - 36)
        $lblSummary.SetBounds(8, 8, $buildWidth - 16, 24)
        $grid.SetBounds(8, 36, $buildWidth - 16, $gridHeight)
        $btnModified.SetBounds(8, $toolbarY, 115, 32)
        $btnClear.SetBounds(132, $toolbarY, 100, 32)
        $btnRefresh.SetBounds(241, $toolbarY, 100, 32)
        $btnRestore.SetBounds(350, $toolbarY, 130, 32)
        $btnOpenBuild.SetBounds(489, $toolbarY, 105, 32)
        $btnBuild.SetBounds($buildWidth - 158, $toolbarY - 2, 150, 36)
        $log.SetBounds(8, $logY, $buildWidth - 16, $logHeight)
    }

    $mmatWidth = $pageMmat.ClientSize.Width
    $mmatHeight = $pageMmat.ClientSize.Height
    if ($mmatWidth -gt 300 -and $mmatHeight -gt 180) {
        $mmatButtonX = $mmatWidth - 192
        $lblMmatObject.SetBounds(12, 16, 70, 24)
        $txtMmatObject.SetBounds(90, 12, [Math]::Max(120, $mmatButtonX - 104), 27)
        $btnClearMmatA4.SetBounds($mmatButtonX, 10, 180, 32)
        $lblMmatSummary.SetBounds(12, 50, $mmatWidth - 24, 24)
        $mmatGrid.SetBounds(12, 78, $mmatWidth - 24, [Math]::Max(80, $mmatHeight - 90))
    }

    $clothWidth = $pageCloth.ClientSize.Width
    $clothHeight = $pageCloth.ClientSize.Height
    if ($clothWidth -gt 300 -and $clothHeight -gt 180) {
        $clothButtonX = $clothWidth - 112
        $lblClothObject.SetBounds(12, 16, 70, 24)
        $txtClothObject.SetBounds(90, 12, [Math]::Max(120, $clothButtonX - 104), 27)
        $btnChooseCloth.SetBounds($clothButtonX, 10, 100, 32)
        $lblClothSummary.SetBounds(12, 50, $clothWidth - 24, 24)
        $clothHeaderGrid.SetBounds(12, 78, $clothWidth - 24, 130)
        $clothGrid.SetBounds(12, 216, $clothWidth - 24, [Math]::Max(80, $clothHeight - 228))
    }
}

$pageBuild.Add_SizeChanged({ Update-WorkspaceEditorLayout })
$pageMmat.Add_SizeChanged({ Update-WorkspaceEditorLayout })
$pageCloth.Add_SizeChanged({ Update-WorkspaceEditorLayout })
$tabs.Add_SelectedIndexChanged({ Update-WorkspaceEditorLayout })
$form.Add_Shown({ Update-WorkspaceEditorLayout })
Update-WorkspaceEditorLayout

$script:context = $null
$script:mmatOperation = $null
$script:mmatOperationKey = ""
$script:clothOperation = $null
$script:clothOperationKey = ""
function Add-Log([string]$Message) {
    $log.AppendText($Message + "`r`n")
    $log.SelectionStart = $log.TextLength
    $log.ScrollToCaret()
}

function Refresh-MmatEditor {
    $mmatGrid.Rows.Clear()
    $btnClearMmatA4.Enabled = $false
    if ($null -eq $script:mmatOperation) {
        $txtMmatObject.Text = $B.no_edit_object
        $lblMmatSummary.Text = $B.mmat_no_selection
        return
    }

    $txtMmatObject.Text = [string]$script:mmatOperation.JsonPath
    if (-not $script:mmatOperation.Available -or
        -not (Test-Path -LiteralPath $script:mmatOperation.JsonPath -PathType Leaf)) {
        $lblMmatSummary.Text = $B.state_missing
        return
    }

    try {
        $material = ConvertFrom-Json ([IO.File]::ReadAllText($script:mmatOperation.JsonPath, [Text.Encoding]::UTF8))
        $entries = @($material.Entries1)
        $a4Total = 0
        for ($index = 0; $index -lt $entries.Count; $index++) {
            $entry = $entries[$index]
            if ($null -eq $entry) { continue }
            $hasA4 = $entry.PSObject.Properties.Name -contains "A4" -and $null -ne $entry.A4
            $a4Count = if ($hasA4) { @($entry.A4).Count } else { 0 }
            if ($hasA4) { $a4Total++ }
            $textureNames = @($entry.A2 | ForEach-Object {
                if ($null -ne $_ -and $_.PSObject.Properties.Name -contains "Name") { [string]$_.Name }
            }) -join ", "
            $rowIndex = $mmatGrid.Rows.Add(
                $index, [string]$entry.A5, @($entry.A1).Count, @($entry.A2).Count,
                @($entry.A3).Count, $a4Count, $textureNames
            )
            $row = $mmatGrid.Rows[$rowIndex]
            $row.Cells["Textures"].ToolTipText = $textureNames
            if ($hasA4) {
                $row.Cells["A4"].Style.ForeColor = [Drawing.Color]::DarkRed
                $row.Cells["A4"].Style.BackColor = [Drawing.Color]::MistyRose
            }
        }
        $entries2Count = @($material.Entries2).Count
        $lblMmatSummary.Text = "$($B.mmat_entries) $($entries.Count) | Entries2 $entries2Count | $($B.a4_blocks) $a4Total"
        $btnClearMmatA4.Enabled = $a4Total -gt 0
    } catch {
        $lblMmatSummary.Text = "$($B.a4_json_error): $($_.Exception.Message)"
    }
}

function Open-MmatEditor([object]$Operation) {
    if ($null -eq $Operation -or [string]$Operation.Kind -ne "mmat") { return }
    $script:mmatOperation = $Operation
    $script:mmatOperationKey = Get-OperationKey $Operation
    Refresh-MmatEditor
    $tabs.SelectedTab = $pageMmat
}

function Set-ClothGridColumns([object[]]$Definitions) {
    $clothGrid.Columns.Clear()
    foreach ($definition in $Definitions) {
        $column = New-Object Windows.Forms.DataGridViewTextBoxColumn
        $column.Name = [string]$definition.Name
        $column.HeaderText = [string]$definition.Header
        if ($definition.ContainsKey("Fill") -and $definition.Fill) {
            $column.AutoSizeMode = "Fill"
            $column.MinimumWidth = if ($definition.ContainsKey("Width")) { [int]$definition.Width } else { 120 }
        } else {
            $column.Width = [int]$definition.Width
        }
        $column.ReadOnly = $true
        $column.SortMode = "NotSortable"
        [void]$clothGrid.Columns.Add($column)
    }
}

function Add-ClothHeaderRow([string]$Name, [object]$Value) {
    [void]$clothHeaderGrid.Rows.Add($Name, [string]$Value)
}

function Refresh-ClothEditor {
    $clothHeaderGrid.Rows.Clear()
    $clothGrid.Rows.Clear()
    $clothGrid.Columns.Clear()
    if ($null -eq $script:clothOperation) {
        $txtClothObject.Text = $B.no_edit_object
        $lblClothSummary.Text = $B.cloth_no_selection
        return
    }

    $path = [string]$script:clothOperation.XmlPath
    $txtClothObject.Text = $path
    if (-not $script:clothOperation.Available -or -not (Test-Path -LiteralPath $path -PathType Leaf)) {
        $lblClothSummary.Text = $B.state_missing
        return
    }

    try {
        [xml]$xml = [IO.File]::ReadAllText($path, [Text.Encoding]::UTF8)
        $category = [string]$script:clothOperation.ClothCategory
        if (-not $category) {
            $category = switch ($xml.DocumentElement.Name) {
                "CLOTH" { "clp" }
                "CLOTH_AT" { "clh" }
                "SeqRoot" { "sequence" }
                "RESET_MOT_LIST" { "reset" }
                default { "other" }
            }
        }
        Add-ClothHeaderRow $B.quick_file_name ([IO.Path]::GetFileName($path))
        Add-ClothHeaderRow $B.quick_type $category

        switch ($category) {
            "clp" {
                $header = $xml.CLOTH.CLOTH_HEADER
                foreach ($child in @($header.ChildNodes)) {
                    if ($child.NodeType -eq [Xml.XmlNodeType]::Element) {
                        Add-ClothHeaderRow $child.Name $child.InnerText
                    }
                }
                $nodes = @($xml.CLOTH.CLOTH_WK_LIST.CLOTH_WK)
                $roots = @($nodes | Where-Object { [int]$_.noUp -eq 4095 }).Count
                $sideLinks = @($nodes | Where-Object { [int]$_.noSide -ne 4095 }).Count
                $lblClothSummary.Text = "$($B.cloth_nodes) $($nodes.Count) | $($B.cloth_roots) $roots | $($B.cloth_side_links) $sideLinks"
                Set-ClothGridColumns @(
                    @{ Name="No"; Header="no"; Width=65 }, @{ Name="Up"; Header="noUp"; Width=65 },
                    @{ Name="Down"; Header="noDown"; Width=70 }, @{ Name="Side"; Header="noSide"; Width=70 },
                    @{ Name="Poly"; Header="noPoly"; Width=70 }, @{ Name="Fix"; Header="noFix"; Width=65 },
                    @{ Name="Rot"; Header="rotLimit"; Width=85 }, @{ Name="Friction"; Header="friction"; Width=80 },
                    @{ Name="Weight"; Header="weight"; Width=75 }, @{ Name="Thick"; Header="thick"; Width=75 },
                    @{ Name="Wind"; Header="windForceArea"; Width=105 }, @{ Name="Offset"; Header="offset"; Width=180; Fill=$true }
                )
                foreach ($node in $nodes) {
                    [void]$clothGrid.Rows.Add(
                        $node.no, $node.noUp, $node.noDown, $node.noSide, $node.noPoly, $node.noFix,
                        $node.rotLimit, $node.friction, $node.weight_, $node.thick_, $node.windForceArea_, $node.offset
                    )
                }
            }
            "clh" {
                $collisions = @($xml.CLOTH_AT.ClothCollision_LIST.ClothCollision)
                Add-ClothHeaderRow "CLOTH_AT_NUM" $xml.CLOTH_AT.CLOTH_AT_NUM
                $lblClothSummary.Text = "$($B.cloth_collisions) $($collisions.Count)"
                Set-ClothGridColumns @(
                    @{ Name="Id"; Header="id"; Width=55 }, @{ Name="P1"; Header="p1"; Width=65 },
                    @{ Name="P2"; Header="p2"; Width=65 }, @{ Name="Capsule"; Header="capsule"; Width=70 },
                    @{ Name="Radius"; Header="radius"; Width=75 }, @{ Name="Weight"; Header="weight"; Width=75 },
                    @{ Name="Offset1"; Header="offset1"; Width=180 }, @{ Name="Offset2"; Header="offset2"; Width=180; Fill=$true },
                    @{ Name="Battle"; Header="battle off"; Width=80 }, @{ Name="Idle"; Header="idle off"; Width=70 }
                )
                foreach ($collision in $collisions) {
                    [void]$clothGrid.Rows.Add(
                        $collision.id_, $collision.p1, $collision.p2, $collision.capsule,
                        $collision.radius, $collision.weight, $collision.offset1, $collision.offset2,
                        $collision.notUseInBattle, $collision.notUseInIdle
                    )
                }
            }
            "sequence" {
                $sequences = @($xml.SeqRoot.ClothTrack.Seq)
                Add-ClothHeaderRow "SeqNum" $xml.SeqRoot.ClothTrack.SeqNum
                $lblClothSummary.Text = "$($B.cloth_sequence_events) $($sequences.Count)"
                Set-ClothGridColumns @(
                    @{ Name="Start"; Header="StartTime"; Width=85 }, @{ Name="FileId"; Header="FileId"; Width=60 },
                    @{ Name="Collisions"; Header="CollisionIds"; Width=155 }, @{ Name="Scale"; Header="ScaleRate"; Width=75 },
                    @{ Name="Fade"; Header="FadeInFrame"; Width=90 }, @{ Name="Floor"; Header="FloorOffset"; Width=85 },
                    @{ Name="FloorFade"; Header="FloorFade"; Width=80 }, @{ Name="Flag"; Header="SeqFlag"; Width=65 },
                    @{ Name="Layer"; Header="LayerFlag"; Width=105; Fill=$true }
                )
                foreach ($sequence in $sequences) {
                    $collisionIds = @($sequence.Attributes | Where-Object {
                        $_.Name -like "CollisionId*" -and [int]$_.Value -ge 0
                    } | Sort-Object Name | ForEach-Object { $_.Value }) -join ", "
                    [void]$clothGrid.Rows.Add(
                        $sequence.StartTime, $sequence.FileId, $collisionIds, $sequence.ScaleRate,
                        $sequence.FadeInFrame, $sequence.FloorAdditiveOffset,
                        $sequence.FloorAdditiveOffsetFadeInFrame, $sequence.SeqFlag, $sequence.LayerFlag
                    )
                }
            }
            "reset" {
                $motions = @($xml.RESET_MOT_LIST.MOT_LIST.RESET_MOT_LIST_WK)
                Add-ClothHeaderRow "RESET_MOT_NUM" $xml.RESET_MOT_LIST.RESET_MOT_NUM
                $lblClothSummary.Text = "$($B.cloth_reset_motions) $($motions.Count)"
                Set-ClothGridColumns @(
                    @{ Name="Front"; Header="frontMot"; Width=220 },
                    @{ Name="Back"; Header="backMot"; Width=220; Fill=$true }
                )
                foreach ($motion in $motions) { [void]$clothGrid.Rows.Add($motion.frontMot, $motion.backMot) }
            }
            default {
                $lblClothSummary.Text = $xml.DocumentElement.Name
                Set-ClothGridColumns @(
                    @{ Name="Property"; Header=$B.col_quick_property; Width=220 },
                    @{ Name="Value"; Header=$B.col_quick_value; Width=300; Fill=$true }
                )
                foreach ($child in @($xml.DocumentElement.ChildNodes)) {
                    [void]$clothGrid.Rows.Add($child.Name, $child.InnerText)
                }
            }
        }
    } catch {
        $lblClothSummary.Text = "$($B.cloth_xml_error): $($_.Exception.Message)"
    }
}

function Open-ClothEditor([object]$Operation) {
    if ($null -eq $Operation -or [string]$Operation.Kind -ne "cloth") { return }
    $script:clothOperation = $Operation
    $script:clothOperationKey = Get-OperationKey $Operation
    Refresh-ClothEditor
    $tabs.SelectedTab = $pageCloth
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

            $editEnabled = [string]$operation.Kind -in @("mmat", "cloth") -and $operation.Available
            $editLabel = if ($editEnabled) { $B.edit } else { "" }

            $rowIndex = $grid.Rows.Add(
                $isChecked, $state, $operation.TypeLabel, $operation.InputLabel,
                $operation.OutputLabel, $B.restore_row, $editLabel
            )
            $row = $grid.Rows[$rowIndex]
            $row.Tag = $operation
            $row.Cells["Input"].ToolTipText = $operation.InputLabel
            $row.Cells["Output"].ToolTipText = $operation.OutputLabel
            $row.Cells["Restore"].Style.ForeColor = [Drawing.SystemColors]::ControlText
            if (-not $editEnabled) {
                $textCell = New-Object Windows.Forms.DataGridViewTextBoxCell
                $textCell.Value = $editLabel
                $row.Cells["Edit"] = $textCell
                $row.Cells["Edit"].ReadOnly = $true
            } else {
                $row.Cells["Edit"].Style.ForeColor = [Drawing.SystemColors]::ControlText
            }
            if (-not $operation.Available) { $row.DefaultCellStyle.ForeColor = [Drawing.Color]::DarkOrange }
            elseif (-not $operation.Changed) { $row.DefaultCellStyle.ForeColor = [Drawing.Color]::Gray }
        }
        $grid.ResumeLayout()
        if ($PreserveSelection) { Add-Log $B.workspace_refreshed }
        else { Add-Log "$($B.workspace_loaded): $($script:context.Root)" }
        if ($script:mmatOperationKey) {
            $script:mmatOperation = @($script:context.Operations | Where-Object {
                (Get-OperationKey $_) -eq $script:mmatOperationKey
            } | Select-Object -First 1)[0]
            Refresh-MmatEditor
        }
        if ($script:clothOperationKey) {
            $script:clothOperation = @($script:context.Operations | Where-Object {
                (Get-OperationKey $_) -eq $script:clothOperationKey
            } | Select-Object -First 1)[0]
            Refresh-ClothEditor
        }
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

    if ($columnName -eq "Edit" -and $operation.Available) {
        if ([string]$operation.Kind -eq "mmat") { Open-MmatEditor $operation }
        elseif ([string]$operation.Kind -eq "cloth") { Open-ClothEditor $operation }
        return
    }
})
$btnClearMmatA4.Add_Click({
    if ($null -eq $script:mmatOperation -or $null -eq $script:context) { return }
    try {
        $a4Count = Get-WorkspaceMaterialA4Count $script:context $script:mmatOperation
        if ($a4Count -le 0) { return }
        $answer = [Windows.Forms.MessageBox]::Show(
            "$($B.confirm_clear_a4_prefix) $a4Count $($B.confirm_clear_a4_suffix):`r`n`r`n$($script:mmatOperation.InputLabel)",
            $B.confirm_edit_title, "OKCancel", "Warning"
        )
        if ($answer -ne "OK") { return }
        $operationKey = $script:mmatOperationKey
        $removed = Remove-WorkspaceMaterialA4 $script:context $script:mmatOperation
        Add-Log "[OK] $($B.a4_removed): $($script:mmatOperation.InputLabel) ($removed)"
        foreach ($buildRow in @($grid.Rows)) {
            if ($null -ne $buildRow.Tag -and (Get-OperationKey $buildRow.Tag) -eq $operationKey) {
                $buildRow.Cells["Selected"].Value = $true
                break
            }
        }
        Load-Manifest $txtManifest.Text -PreserveSelection
    } catch {
        Add-Log "[$($B.failed)] $($script:mmatOperation.InputLabel): $($_.Exception.Message)"
        [Windows.Forms.MessageBox]::Show($_.Exception.Message, $B.a4_remove_failed, "OK", "Error") | Out-Null
    }
})
$btnChooseCloth.Add_Click({
    $dialog = New-Object Windows.Forms.OpenFileDialog
    $dialog.Title = $B.select_cloth_object
    $dialog.Filter = "GBFR cloth XML (*.bxm.xml;*.xml)|*.bxm.xml;*.xml|All files (*.*)|*.*"
    if ($null -ne $script:context) {
        $clothDir = Join-Path $script:context.Root "unpack\data\pl\$($script:context.Workspace.CharacterId)"
        if (Test-Path -LiteralPath $clothDir -PathType Container) { $dialog.InitialDirectory = $clothDir }
    }
    if ($dialog.ShowDialog() -eq "OK") {
        $manualOperation = [PSCustomObject]@{
            Kind = "cloth"
            OutputLabel = $dialog.FileName
            XmlPath = $dialog.FileName
            Available = $true
            ClothCategory = ""
        }
        Open-ClothEditor $manualOperation
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
    $form.Opacity = 0
    $form.Show()
    [Windows.Forms.Application]::DoEvents()
    $form.ClientSize = New-Object Drawing.Size(1600, 900)
    $form.PerformLayout()
    $tabs.PerformLayout()
    [Windows.Forms.Application]::DoEvents()
    Update-WorkspaceEditorLayout
    $layoutOk = $grid.Bottom -lt $btnModified.Top -and
        $btnModified.Bottom -lt $log.Top -and
        $grid.Right -le $pageBuild.ClientSize.Width -and
        $log.Bottom -le $pageBuild.ClientSize.Height
    $actionColumnsVisible = $grid.Columns["Restore"].Displayed -and $grid.Columns["Edit"].Displayed
    if (-not $layoutOk) {
        throw "Build page layout overlap: page=$($pageBuild.ClientSize), grid=$($grid.Bounds), toolbar=$($btnModified.Bounds), log=$($log.Bounds)"
    }
    if (-not $actionColumnsVisible) { throw "Restore/Edit columns are not displayed in the resized build grid" }
    $mmatRows = @($grid.Rows | Where-Object { $null -ne $_.Tag -and [string]$_.Tag.Kind -eq "mmat" })
    $clothRows = @($grid.Rows | Where-Object { $null -ne $_.Tag -and [string]$_.Tag.Kind -eq "cloth" })
    $restoreButtons = @($grid.Rows | Where-Object { $_.Cells["Restore"].Value -eq $B.restore_row })
    $mmatEditButtons = @($mmatRows | Where-Object { $_.Cells["Edit"].Value -eq $B.edit })
    $clothEditButtons = @($clothRows | Where-Object { $_.Cells["Edit"].Value -eq $B.edit })
    if ($mmatRows.Count -gt 0) { Open-MmatEditor $mmatRows[0].Tag }
    $mmatTabSelected = $tabs.SelectedTab -eq $pageMmat
    $clothViews = [System.Collections.Generic.List[string]]::new()
    foreach ($category in @("clp", "clh", "sequence", "reset")) {
        $categoryRow = $clothRows | Where-Object { [string]$_.Tag.ClothCategory -eq $category } | Select-Object -First 1
        if ($null -eq $categoryRow) { continue }
        Open-ClothEditor $categoryRow.Tag
        if ($clothGrid.Rows.Count -eq 0) { throw "Cloth $category view contains no rows" }
        $clothViews.Add("${category}:$($clothGrid.Rows.Count)")
    }
    $clothTabSelected = if ($clothRows.Count -gt 0) { $tabs.SelectedTab -eq $pageCloth } else { $true }
    Write-Host "UI smoke: layout=$layoutOk, actionColumns=$actionColumnsVisible, page=$($pageBuild.ClientSize.Width)x$($pageBuild.ClientSize.Height), grid=$($grid.Width)x$($grid.Height), rows=$($grid.Rows.Count), restore=$($restoreButtons.Count), bulkRestore=$($btnRestore.Text -eq $B.restore_selected), mmatEdit=$($mmatEditButtons.Count), clothEdit=$($clothEditButtons.Count), mmatEntries=$($mmatGrid.Rows.Count), mmatTab=$mmatTabSelected, clothTab=$clothTabSelected, clothViews=$($clothViews -join ',')"
    $form.Close()
    exit 0
}
[void]$form.ShowDialog()
