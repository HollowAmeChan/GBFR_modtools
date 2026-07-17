Set-StrictMode -Version Latest

function Get-WorkspaceSha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function ConvertTo-WorkspacePath([string]$Path) {
    return $Path.Replace('\', '/')
}

function Resolve-WorkspaceFile([string]$WorkspaceRoot, [string]$RelativePath) {
    $root = [IO.Path]::GetFullPath($WorkspaceRoot).TrimEnd([char[]]@('\', '/'))
    $full = [IO.Path]::GetFullPath((Join-Path $root ($RelativePath.Replace('/', '\'))))
    $prefix = $root + [IO.Path]::DirectorySeparatorChar
    if (-not $full.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Workspace path escapes its root: $RelativePath"
    }
    return $full
}

function Assert-Wtb([byte[]]$Bytes, [string]$Path) {
    if ($Bytes.Length -lt 32 -or
        $Bytes[0] -ne 0x57 -or $Bytes[1] -ne 0x54 -or
        $Bytes[2] -ne 0x42 -or $Bytes[3] -ne 0x00) {
        throw "Not a supported WTB texture: $Path"
    }

    $count = [BitConverter]::ToUInt32($Bytes, 4)
    $offsetTable = [BitConverter]::ToUInt32($Bytes, 12)
    $sizeTable = [BitConverter]::ToUInt32($Bytes, 16)
    if ($count -eq 0 -or $count -gt 4096) { throw "Invalid WTB slot count: $count ($Path)" }
    if (($offsetTable + $count * 4) -gt $Bytes.Length -or
        ($sizeTable + $count * 4) -gt $Bytes.Length) {
        throw "WTB tables extend beyond the file: $Path"
    }

    return [PSCustomObject]@{
        Count       = [int]$count
        OffsetTable = [int]$offsetTable
        SizeTable   = [int]$sizeTable
    }
}

function Expand-WtbTexture([string]$TexturePath, [string]$OutputDirectory) {
    $bytes = [IO.File]::ReadAllBytes($TexturePath)
    $header = Assert-Wtb $bytes $TexturePath
    New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

    $baseName = [IO.Path]::GetFileNameWithoutExtension($TexturePath)
    $results = [System.Collections.Generic.List[PSCustomObject]]::new()
    for ($i = 0; $i -lt $header.Count; $i++) {
        $offset = [BitConverter]::ToUInt32($bytes, $header.OffsetTable + $i * 4)
        $size = [BitConverter]::ToUInt32($bytes, $header.SizeTable + $i * 4)
        if ($offset -eq 0 -or $size -eq 0) { continue }
        if (($offset + $size) -gt $bytes.Length) {
            throw "WTB slot $i extends beyond the file: $TexturePath"
        }

        $dds = New-Object byte[] ([int]$size)
        [Array]::Copy($bytes, [int]$offset, $dds, 0, [int]$size)
        if ($dds.Length -lt 4 -or [Text.Encoding]::ASCII.GetString($dds, 0, 4) -ne 'DDS ') {
            throw "WTB slot $i is not a DDS payload: $TexturePath"
        }

        $outputPath = Join-Path $OutputDirectory "${baseName}_${i}.dds"
        [IO.File]::WriteAllBytes($outputPath, $dds)
        $results.Add([PSCustomObject]@{
            Index  = $i
            Path   = $outputPath
            Sha256 = Get-WorkspaceSha256 $outputPath
        })
    }
    return $results
}

function Convert-MmatToJson(
    [string]$FlatcPath,
    [string]$SchemaPath,
    [string]$MmatPath,
    [string]$JsonPath
) {
    $outputDir = [IO.Path]::GetDirectoryName($JsonPath)
    New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
    $flatcOutput = Join-Path $outputDir ([IO.Path]::GetFileNameWithoutExtension($MmatPath) + '.json')
    $oldErrorAction = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $result = & $FlatcPath --json --strict-json --raw-binary -o $outputDir $SchemaPath -- $MmatPath 2>&1
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $oldErrorAction
    }
    if ($exitCode -ne 0 -or -not (Test-Path -LiteralPath $flatcOutput)) {
        throw "flatc decode failed for $MmatPath`n$($result -join [Environment]::NewLine)"
    }
    Move-Item -LiteralPath $flatcOutput -Destination $JsonPath -Force
    return $JsonPath
}

function Convert-BxmToXml(
    [string]$GbfrDataToolsPath,
    [string]$BxmPath,
    [string]$XmlPath
) {
    if (-not (Test-Path -LiteralPath $GbfrDataToolsPath -PathType Leaf)) {
        throw "GBFRDataTools.exe not found: $GbfrDataToolsPath"
    }
    if (-not (Test-Path -LiteralPath $BxmPath -PathType Leaf)) {
        throw "BXM source not found: $BxmPath"
    }
    $outputDir = [IO.Path]::GetDirectoryName($XmlPath)
    New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

    $oldErrorAction = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $result = @(& $GbfrDataToolsPath bxm-to-xml -i $BxmPath -o $XmlPath 2>&1)
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $oldErrorAction
    }
    if ($exitCode -ne 0 -or -not (Test-Path -LiteralPath $XmlPath -PathType Leaf)) {
        throw "BXM decode failed for $BxmPath`n$($result -join [Environment]::NewLine)"
    }
    try {
        [xml]([IO.File]::ReadAllText($XmlPath, [Text.Encoding]::UTF8)) | Out-Null
    } catch {
        throw "BXM decoder produced invalid XML for $BxmPath`: $($_.Exception.Message)"
    }
    return $XmlPath
}
