param(
    [string]$ManifestPath = "",
    [switch]$BuildChanged
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$libRoot = Join-Path $PSScriptRoot "_lib"
$flatcExe = Join-Path $libRoot "flatc.exe"
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
        if ($existingSlots.Count -eq 0) { $missing++; continue }

        $operations.Add([PSCustomObject]@{
            Kind = "texture"
            TypeLabel = $B.type_texture
            InputLabel = (@($existingSlots | ForEach-Object { [IO.Path]::GetFileName($_.Path) }) -join ", ")
            OutputLabel = [string]$texture.Output
            Changed = $changed
            Record = $texture
            Slots = @($existingSlots)
        })
    }

    foreach ($material in @($workspace.Materials)) {
        $jsonPath = Resolve-WorkspaceFile $root ([string]$material.Json)
        if (-not (Test-Path -LiteralPath $jsonPath -PathType Leaf)) { $missing++; continue }
        $changed = (Get-WorkspaceSha256 $jsonPath) -ne [string]$material.BaselineSha256
        $operations.Add([PSCustomObject]@{
            Kind = "mmat"
            TypeLabel = $B.type_mmat
            InputLabel = [IO.Path]::GetFileName($jsonPath)
            OutputLabel = [string]$material.Output
            Changed = $changed
            Record = $material
            JsonPath = $jsonPath
        })
    }

    if ($workspace.PSObject.Properties.Name -contains "NewTextures") {
        foreach ($texture in @($workspace.NewTextures)) {
            $inputPath = Resolve-WorkspaceFile $root ([string]$texture.Input)
            if (-not (Test-Path -LiteralPath $inputPath -PathType Leaf)) { $missing++; continue }
            $changed = (Get-WorkspaceSha256 $inputPath) -ne [string]$texture.BaselineSha256
            $operations.Add([PSCustomObject]@{
                Kind = "new_texture"
                TypeLabel = $B.type_new_texture
                InputLabel = [IO.Path]::GetFileName($inputPath)
                OutputLabel = [string]$texture.Output
                Changed = $changed
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

if ($BuildChanged) {
    if (-not $ManifestPath) { throw $B.err_build_changed_manifest }
    $context = Get-WorkspaceOperations $ManifestPath
    $selected = @($context.Operations | Where-Object { $_.Changed })
    $result = Invoke-WorkspaceBuild $context $selected { param($message) Write-Host $message }
    Write-Host "$($B.build_finished): $($B.success) $($result.Success), $($B.failed) $($result.Failed)"
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

$btnOpenBuild = New-Object Windows.Forms.Button
$btnOpenBuild.Text = $B.open_build
$btnOpenBuild.Location = New-Object Drawing.Point(247, 484)
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

function Load-Manifest([string]$Path) {
    try {
        $script:context = Get-WorkspaceOperations $Path
        $txtManifest.Text = [IO.Path]::GetFullPath($Path)
        $list.Items.Clear()
        foreach ($operation in @($script:context.Operations)) {
            $state = if ($operation.Changed) { $B.state_modified } else { $B.state_unchanged }
            $item = New-Object Windows.Forms.ListViewItem($state)
            [void]$item.SubItems.Add($operation.TypeLabel)
            [void]$item.SubItems.Add($operation.InputLabel)
            [void]$item.SubItems.Add($operation.OutputLabel)
            $item.Tag = $operation
            $item.Checked = $operation.Changed
            if (-not $operation.Changed) { $item.ForeColor = [Drawing.Color]::Gray }
            [void]$list.Items.Add($item)
        }
        Add-Log "$($B.workspace_loaded): $($script:context.Root)"
        Update-SelectionSummary
    } catch {
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
    $targets = ($selected | ForEach-Object { "- $($_.OutputLabel)" }) -join "`r`n"
    $answer = [Windows.Forms.MessageBox]::Show(
        "$($B.confirm_prefix) $($selected.Count) $($B.confirm_suffix):`r`n`r`n$targets",
        $B.confirm_title, "OKCancel", "Information"
    )
    if ($answer -ne "OK") { return }
    $btnBuild.Enabled = $false
    $result = Invoke-WorkspaceBuild $script:context $selected { param($message) Add-Log $message }
    Add-Log "$($B.build_finished): $($B.success) $($result.Success), $($B.failed) $($result.Failed)"
    Update-SelectionSummary
    if ($result.Failed -gt 0) {
        [Windows.Forms.MessageBox]::Show($B.partial_failure, $B.build_finished, "OK", "Warning") | Out-Null
    }
})

if ($ManifestPath -and (Test-Path -LiteralPath $ManifestPath)) { Load-Manifest $ManifestPath }
[void]$form.ShowDialog()
