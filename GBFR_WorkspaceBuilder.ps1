param(
    [string]$ManifestPath = "",
    [switch]$BuildChanged,
    [switch]$RestoreChanged
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

$list = New-Object Windows.Forms.ListView
$list.Location = New-Object Drawing.Point(14, 82)
$list.Size = New-Object Drawing.Size(948, 390)
$list.Anchor = "Top,Bottom,Left,Right"
$list.View = "Details"
$list.CheckBoxes = $true
$list.FullRowSelect = $true
$list.GridLines = $true
$list.HideSelection = $false
[void]$list.Columns.Add($B.col_state, 80)
[void]$list.Columns.Add($B.col_operation, 100)
[void]$list.Columns.Add($B.col_input, 280)
[void]$list.Columns.Add($B.col_output, 450)
$form.Controls.Add($list)

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
    if ($null -eq $script:context) { $btnBuild.Enabled = $false; return }
    $checked = @($list.CheckedItems).Count
    $btnBuild.Enabled = $checked -gt 0
    $changed = @($script:context.Operations | Where-Object { $_.Changed }).Count
    $lblSummary.Text = "$($script:context.Workspace.CharacterId) | $($B.candidate) $($script:context.Operations.Count) | $($B.modified_count) $changed | $($B.selected_count) $checked | $($B.missing_count) $($script:context.MissingCount)"
}

function Get-OperationKey([object]$Operation) {
    return "$($Operation.Kind)|$($Operation.OutputLabel)"
}

function Load-Manifest([string]$Path, [switch]$PreserveSelection) {
    try {
        $checkedKeys = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        $knownKeys = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        if ($PreserveSelection) {
            foreach ($existingItem in @($list.Items)) {
                $key = Get-OperationKey $existingItem.Tag
                $knownKeys.Add($key) | Out-Null
                if ($existingItem.Checked) { $checkedKeys.Add($key) | Out-Null }
            }
        }

        $script:context = Get-WorkspaceOperations $Path
        $txtManifest.Text = [IO.Path]::GetFullPath($Path)
        $list.BeginUpdate()
        $list.Items.Clear()
        foreach ($operation in @($script:context.Operations)) {
            $state = if (-not $operation.Available) { $B.state_missing }
                     elseif ($operation.Changed) { $B.state_modified }
                     else { $B.state_unchanged }
            $item = New-Object Windows.Forms.ListViewItem($state)
            [void]$item.SubItems.Add($operation.TypeLabel)
            [void]$item.SubItems.Add($operation.InputLabel)
            [void]$item.SubItems.Add($operation.OutputLabel)
            $item.Tag = $operation
            $key = Get-OperationKey $operation
            $item.Checked = if ($PreserveSelection -and $knownKeys.Contains($key)) {
                $checkedKeys.Contains($key)
            } else {
                [bool]$operation.Changed
            }
            if (-not $operation.Available) { $item.ForeColor = [Drawing.Color]::DarkOrange }
            elseif (-not $operation.Changed) { $item.ForeColor = [Drawing.Color]::Gray }
            [void]$list.Items.Add($item)
        }
        $list.EndUpdate()
        if ($PreserveSelection) { Add-Log $B.workspace_refreshed }
        else { Add-Log "$($B.workspace_loaded): $($script:context.Root)" }
        Update-SelectionSummary
    } catch {
        try { $list.EndUpdate() } catch {}
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

$list.Add_ItemChecked({
    if ($form.IsHandleCreated) {
        $form.BeginInvoke([Action]{ Update-SelectionSummary }) | Out-Null
    }
})
$btnModified.Add_Click({
    foreach ($item in $list.Items) { $item.Checked = [bool]$item.Tag.Changed }
    Update-SelectionSummary
})
$btnClear.Add_Click({ foreach ($item in $list.Items) { $item.Checked = $false }; Update-SelectionSummary })
$btnRefresh.Add_Click({
    if ($txtManifest.Text) { Load-Manifest $txtManifest.Text -PreserveSelection }
})
$btnRestore.Add_Click({
    if ($null -eq $script:context) { return }
    $selectedItems = @($list.CheckedItems)
    $selected = @($selectedItems | ForEach-Object { $_.Tag })
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
    foreach ($item in $selectedItems) { $item.Checked = $false }
    Load-Manifest $txtManifest.Text -PreserveSelection
    $btnRestore.Enabled = $true
    if ($result.Failed -gt 0) {
        [Windows.Forms.MessageBox]::Show($B.partial_failure, $B.restore_finished, "OK", "Warning") | Out-Null
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
    $selected = @($list.CheckedItems | ForEach-Object { $_.Tag })
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
[void]$form.ShowDialog()
