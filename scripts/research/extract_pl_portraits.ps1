<#
.SYNOPSIS
Extracts numbered player character portraits as PNG files.

.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts/research/extract_pl_portraits.ps1 `
  -GameDataRoot "D:\Steam\steamapps\common\Granblue Fantasy Relink\data"
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$GameDataRoot,

    [string]$OutputDirectory = "",

    [ValidateSet("FHD", "4K")]
    [string]$Resolution = "FHD"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
. (Join-Path $repoRoot "scripts\workspace\workspace_lib.ps1")

$dataRoot = [IO.Path]::GetFullPath($GameDataRoot).TrimEnd([char[]]@('\', '/'))
if (-not (Test-Path -LiteralPath $dataRoot -PathType Container)) {
    throw "Game data root was not found: $dataRoot"
}

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $suffix = if ($Resolution -eq "FHD") { "fhd" } else { "4k" }
    $OutputDirectory = Join-Path $repoRoot "research_output\pl_portraits_$suffix"
}
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null
Get-ChildItem -LiteralPath $outputRoot -Filter "*.png" -File | Remove-Item -Force

$modelRoot = Join-Path $dataRoot "model\pl"
$portraitRelative = if ($Resolution -eq "FHD") {
    "ui\fhd\layouts\common\image_chara\noatlastextures"
} else {
    "ui\layouts\common\image_chara\noatlastextures"
}
$portraitRoot = Join-Path $dataRoot $portraitRelative
$texconv = Join-Path $repoRoot "_lib\tools\texconv.exe"

foreach ($requiredDirectory in @($modelRoot, $portraitRoot)) {
    if (-not (Test-Path -LiteralPath $requiredDirectory -PathType Container)) {
        throw "Required directory was not found: $requiredDirectory"
    }
}
if (-not (Test-Path -LiteralPath $texconv -PathType Leaf)) {
    throw "texconv.exe was not found. Run build.bat tools first."
}

function Get-PlayerModelIds([string]$Root) {
    $ids = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($file in Get-ChildItem -LiteralPath $Root -Recurse -Filter "*.minfo" -File) {
        if ($file.BaseName -match '^pl(?<id>\d{4})$' -and $file.Directory.Name -ieq $file.BaseName) {
            $null = $ids.Add($Matches.id)
        }
    }
    return @($ids)
}

function Get-BasePortraits([string]$Root) {
    $portraits = @{}
    foreach ($file in Get-ChildItem -LiteralPath $Root -Filter "cmn_imgchr_*.wtb" -File) {
        if ($file.Name -match '^cmn_imgchr_(?<id>\d{4})\.wtb$') {
            $portraits[$Matches.id] = $file
        }
    }
    return $portraits
}

function Export-WtbSlotZero([IO.FileInfo]$Source, [string]$Destination) {
    $bytes = [IO.File]::ReadAllBytes($Source.FullName)
    $header = Assert-Wtb $bytes $Source.FullName
    $offset = [BitConverter]::ToUInt32($bytes, $header.OffsetTable)
    $size = [BitConverter]::ToUInt32($bytes, $header.SizeTable)
    if ($offset -eq 0 -or $size -eq 0 -or ($offset + $size) -gt $bytes.Length) {
        throw "WTB slot 0 is empty or invalid: $($Source.FullName)"
    }

    $dds = New-Object byte[] ([int]$size)
    [Array]::Copy($bytes, [int]$offset, $dds, 0, [int]$size)
    if ($dds.Length -lt 20 -or [Text.Encoding]::ASCII.GetString($dds, 0, 4) -ne 'DDS ') {
        throw "WTB slot 0 is not a DDS payload: $($Source.FullName)"
    }
    [IO.File]::WriteAllBytes($Destination, $dds)
}

$modelIds = @(Get-PlayerModelIds $modelRoot)
$portraits = Get-BasePortraits $portraitRoot
$notes = [System.Collections.Generic.List[string]]::new()
$temporaryRoot = Join-Path ([IO.Path]::GetTempPath()) ("gbfr_pl_portraits_" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $temporaryRoot | Out-Null

try {
    foreach ($id in @($portraits.Keys | Sort-Object)) {
        $hasModel = $modelIds -contains $id
        $source = $portraits[$id]
        $prefix = if ($hasModel) { "pl$id" } else { "ui_only" }
        $baseName = "${prefix}__cmn_imgchr_$id"
        $temporaryDds = Join-Path $temporaryRoot "$baseName.dds"
        Export-WtbSlotZero $source $temporaryDds

        $relativeSource = "data/" + $source.FullName.Substring($dataRoot.Length).TrimStart([char[]]@('\', '/')).Replace('\', '/')
        $modelLabel = if ($hasModel) { "pl$id" } else { "无同号 pl 模型" }
        $notes.Add("| ``$baseName.png`` | $modelLabel | ``$relativeSource`` |")
    }

    $inputs = @(Get-ChildItem -LiteralPath $temporaryRoot -Filter "*.dds" -File | Sort-Object Name | Select-Object -ExpandProperty FullName)
    # The game stores color data in BC7_UNORM without an sRGB DXGI tag. Preserve the samples,
    # but emit PNG metadata as sRGB instead of texconv's otherwise incorrect gAMA=1.0.
    $convertOutput = @(& $texconv -nologo -y -m 1 -srgbi -f R8G8B8A8_UNORM_SRGB -ft png -o $outputRoot @inputs 2>&1)
    if ($LASTEXITCODE -ne 0) {
        throw "texconv PNG conversion failed:`n$($convertOutput -join [Environment]::NewLine)"
    }
} finally {
    if (Test-Path -LiteralPath $temporaryRoot) {
        Remove-Item -LiteralPath $temporaryRoot -Recurse -Force
    }
}

$missingPortraitIds = @($modelIds | Where-Object { -not $portraits.ContainsKey($_) } | Sort-Object)
$uiOnlyIds = @($portraits.Keys | Where-Object { $modelIds -notcontains $_ } | Sort-Object)
$missingPortraitText = if ($missingPortraitIds.Count) { $missingPortraitIds -join ', ' } else { '无' }
$uiOnlyText = if ($uiOnlyIds.Count) { $uiOnlyIds -join ', ' } else { '无' }
$document = [System.Collections.Generic.List[string]]::new()
$document.Add("# PL 角色立绘编号提取")
$document.Add("")
$document.Add("- 分辨率：$Resolution")
$document.Add("- 只读取 ``cmn_imgchr_XXXX.wtb`` 的第 0 槽。")
$document.Add("- ``cmn_imgchr_XXXX_glow.wtb`` 是白色遮罩，本次未提取。")
$document.Add("- 原始 BC7_UNORM 颜色按 sRGB 写入 PNG，避免线性 gamma 元数据造成预览过亮。")
$document.Add("- 有同号模型时，PNG 使用 ``plXXXX__cmn_imgchr_XXXX.png`` 命名。")
$document.Add("- 无同号模型的纯数字 UI 立绘使用 ``ui_only__cmn_imgchr_XXXX.png`` 命名。")
$document.Add("")
$document.Add("同号 pl 模型但没有基础立绘：$missingPortraitText")
$document.Add("")
$document.Add("有基础立绘但没有同号 pl 模型：$uiOnlyText")
$document.Add("")
$document.Add("| PNG | 模型编号 | 源 WTB |")
$document.Add("|---|---|---|")
$document.AddRange($notes)
[IO.File]::WriteAllLines((Join-Path $outputRoot "README.md"), $document, [Text.UTF8Encoding]::new($false))

Write-Host "Extracted $($portraits.Count) base portraits as PNG ($Resolution)." -ForegroundColor Green
Write-Host "Output: $outputRoot"
