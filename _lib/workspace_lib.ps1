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

function Align-Up([long]$Value, [int]$Alignment) {
    return [long]([Math]::Ceiling($Value / [double]$Alignment) * $Alignment)
}

function New-WtbTexture(
    [string]$SourceTexture,
    [hashtable]$EditedSlots,
    [string]$OutputTexture
) {
    $sourceBytes = [IO.File]::ReadAllBytes($SourceTexture)
    $wtb = Assert-Wtb $sourceBytes $SourceTexture

    $dataOffsets = [System.Collections.Generic.List[int]]::new()
    for ($i = 0; $i -lt $wtb.Count; $i++) {
        $offset = [BitConverter]::ToUInt32($sourceBytes, $wtb.OffsetTable + $i * 4)
        $size = [BitConverter]::ToUInt32($sourceBytes, $wtb.SizeTable + $i * 4)
        if ($offset -gt 0 -and $size -gt 0) { $dataOffsets.Add([int]$offset) }
    }
    if ($dataOffsets.Count -eq 0) { throw "WTB contains no DDS payloads: $SourceTexture" }

    $headerLength = ($dataOffsets | Measure-Object -Minimum).Minimum
    $headerBytes = New-Object byte[] $headerLength
    [Array]::Copy($sourceBytes, 0, $headerBytes, 0, $headerLength)

    $payloads = [System.Collections.Generic.List[PSCustomObject]]::new()
    $cursor = [long]$headerLength
    for ($i = 0; $i -lt $wtb.Count; $i++) {
        $originalOffset = [BitConverter]::ToUInt32($sourceBytes, $wtb.OffsetTable + $i * 4)
        $originalSize = [BitConverter]::ToUInt32($sourceBytes, $wtb.SizeTable + $i * 4)
        if ($originalOffset -eq 0 -or $originalSize -eq 0) {
            if ($EditedSlots.ContainsKey($i)) { throw "Cannot populate empty WTB slot $i" }
            continue
        }

        if ($EditedSlots.ContainsKey($i)) {
            $payload = [IO.File]::ReadAllBytes([string]$EditedSlots[$i])
            if ($payload.Length -lt 4 -or [Text.Encoding]::ASCII.GetString($payload, 0, 4) -ne 'DDS ') {
                throw "Edited slot $i is not a DDS file: $($EditedSlots[$i])"
            }
        } else {
            $payload = New-Object byte[] ([int]$originalSize)
            [Array]::Copy($sourceBytes, [int]$originalOffset, $payload, 0, [int]$originalSize)
        }

        $cursor = Align-Up $cursor 4096
        if ($cursor -gt [uint32]::MaxValue -or $payload.Length -gt [uint32]::MaxValue) {
            throw "WTB payload is too large: $SourceTexture"
        }
        $newOffset = [BitConverter]::GetBytes([uint32]$cursor)
        $newSize = [BitConverter]::GetBytes([uint32]$payload.Length)
        [Array]::Copy($newOffset, 0, $headerBytes, $wtb.OffsetTable + $i * 4, 4)
        [Array]::Copy($newSize, 0, $headerBytes, $wtb.SizeTable + $i * 4, 4)
        $payloads.Add([PSCustomObject]@{ Offset = [int]$cursor; Bytes = $payload })
        $cursor += $payload.Length
    }

    $outputBytes = New-Object byte[] ([int]$cursor)
    [Array]::Copy($headerBytes, 0, $outputBytes, 0, $headerBytes.Length)
    foreach ($item in $payloads) {
        [Array]::Copy($item.Bytes, 0, $outputBytes, $item.Offset, $item.Bytes.Length)
    }

    $outputDir = [IO.Path]::GetDirectoryName($OutputTexture)
    New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
    $tempPath = "$OutputTexture.tmp"
    [IO.File]::WriteAllBytes($tempPath, $outputBytes)
    Move-Item -LiteralPath $tempPath -Destination $OutputTexture -Force
    return $OutputTexture
}

function New-WtbTextureFromDds(
    [string]$NierCliPath,
    [string]$DdsPath,
    [string]$OutputTexture,
    [uint32]$TextureId = 0
) {
    if (-not (Test-Path -LiteralPath $NierCliPath -PathType Leaf)) {
        throw "nier_cli_mgrr.exe not found: $NierCliPath"
    }
    $dds = [IO.File]::ReadAllBytes($DdsPath)
    if ($dds.Length -lt 4 -or [Text.Encoding]::ASCII.GetString($dds, 0, 4) -ne 'DDS ') {
        throw "Input is not a DDS file: $DdsPath"
    }

    $tempRoot = Join-Path ([IO.Path]::GetTempPath()) ("gbfr_wtb_" + [Guid]::NewGuid().ToString('N'))
    $inputDir = Join-Path $tempRoot "texture.wtb_extracted"
    $generatedWtb = Join-Path $tempRoot "texture.wtb"
    try {
        New-Item -ItemType Directory -Force -Path $inputDir | Out-Null
        $ddsName = "0_{0:x8}.dds" -f $TextureId
        [IO.File]::WriteAllBytes((Join-Path $inputDir $ddsName), $dds)

        $oldErrorAction = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        try {
            $result = @(& $NierCliPath $inputDir 2>&1)
            $exitCode = $LASTEXITCODE
        } finally {
            $ErrorActionPreference = $oldErrorAction
        }
        if ($exitCode -ne 0 -or -not (Test-Path -LiteralPath $generatedWtb -PathType Leaf)) {
            throw "nier_cli WTB build failed for $DdsPath`n$($result -join [Environment]::NewLine)"
        }

        $outputBytes = [IO.File]::ReadAllBytes($generatedWtb)
        Assert-Wtb $outputBytes $generatedWtb | Out-Null
        $payloadOffsetTable = [BitConverter]::ToUInt32($outputBytes, 12)
        $payloadOffset = [BitConverter]::ToUInt32($outputBytes, $payloadOffsetTable)
        if (($payloadOffset + 4) -gt $outputBytes.Length -or
            [Text.Encoding]::ASCII.GetString($outputBytes, $payloadOffset, 4) -ne 'DDS ') {
            throw "nier_cli output does not contain a DDS payload: $generatedWtb"
        }

        $outputDir = [IO.Path]::GetDirectoryName($OutputTexture)
        New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
        $tempOutput = "$OutputTexture.tmp"
        [IO.File]::WriteAllBytes($tempOutput, $outputBytes)
        Move-Item -LiteralPath $tempOutput -Destination $OutputTexture -Force
        return $OutputTexture
    } finally {
        if (Test-Path -LiteralPath $tempRoot) { Remove-Item -LiteralPath $tempRoot -Recurse -Force }
    }
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

function Convert-JsonToMmat(
    [string]$FlatcPath,
    [string]$SchemaPath,
    [string]$JsonPath,
    [string]$MmatPath
) {
    $tempDir = Join-Path ([IO.Path]::GetTempPath()) ("gbfr_mmat_" + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force -Path $tempDir | Out-Null
    try {
        $oldErrorAction = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        try {
            $result = & $FlatcPath --binary -o $tempDir $SchemaPath $JsonPath 2>&1
            $exitCode = $LASTEXITCODE
        } finally {
            $ErrorActionPreference = $oldErrorAction
        }
        $binName = [IO.Path]::GetFileNameWithoutExtension($JsonPath) + '.bin'
        $binPath = Join-Path $tempDir $binName
        if ($exitCode -ne 0 -or -not (Test-Path -LiteralPath $binPath)) {
            throw "flatc encode failed for $JsonPath`n$($result -join [Environment]::NewLine)"
        }
        $outputDir = [IO.Path]::GetDirectoryName($MmatPath)
        New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
        Move-Item -LiteralPath $binPath -Destination $MmatPath -Force
        return $MmatPath
    } finally {
        if (Test-Path -LiteralPath $tempDir) { Remove-Item -LiteralPath $tempDir -Recurse -Force }
    }
}
