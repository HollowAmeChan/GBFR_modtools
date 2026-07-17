Set-StrictMode -Version Latest

function Import-SopOperationCatalog([string]$Path) {
    $document = ConvertFrom-Json ([IO.File]::ReadAllText($Path, [Text.Encoding]::UTF8))
    $catalog = @{}
    foreach ($operation in @($document.Operations)) {
        $hash = ([string]$operation.Hash).ToLowerInvariant()
        if ($hash -notmatch '^0x[0-9a-f]{8}$') { throw "Invalid SOP catalog hash: $hash" }
        if ($catalog.ContainsKey($hash)) { throw "Duplicate SOP catalog hash: $hash" }
        $catalog[$hash] = $operation
    }
    return $catalog
}

function Import-SopBoneNames([string]$Path) {
    $document = ConvertFrom-Json ([IO.File]::ReadAllText($Path, [Text.Encoding]::UTF8))
    $names = @{}
    foreach ($property in $document.PSObject.Properties) {
        $names[$property.Name.ToLowerInvariant()] = [string]$property.Value
    }
    return $names
}

function Format-SopBoneCode([uint32]$Code) {
    return "_{0:x3}" -f $Code
}

function Format-SopBoneDisplay([string]$Bone, [hashtable]$BoneNames) {
    $key = $Bone.ToLowerInvariant()
    if ($BoneNames.ContainsKey($key)) { return "$($BoneNames[$key]) ($Bone)" }
    return $Bone
}

function Read-SopConstraintReport([string]$Path, [hashtable]$Catalog) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 12 -or $bytes[0] -ne 0x73 -or $bytes[1] -ne 0x6f -or
        $bytes[2] -ne 0x70 -or $bytes[3] -ne 0) { throw "Invalid SOP magic: $Path" }

    $version = [BitConverter]::ToUInt32($bytes, 4)
    if ($version -ne 0x20200309) { throw ("Unsupported SOP version 0x{0:X8}: {1}" -f $version, $Path) }
    $count = [BitConverter]::ToUInt32($bytes, 8)
    if ($count -gt 100000) { throw "Unreasonable SOP operation count: $count" }
    $tableEnd = 12L + 4L * $count
    if ($tableEnd -gt $bytes.Length) { throw "SOP offset table outside file: $Path" }

    $offsets = [System.Collections.Generic.List[uint32]]::new()
    for ($index = 0; $index -lt $count; $index++) {
        $offset = [BitConverter]::ToUInt32($bytes, 12 + 4 * $index)
        if ($offset -lt $tableEnd -or $offset -ge $bytes.Length -or
            ($index -gt 0 -and $offset -le $offsets[$index - 1])) { throw "Invalid SOP operation offset #$index" }
        $offsets.Add($offset)
    }

    $operations = [System.Collections.Generic.List[PSCustomObject]]::new()
    for ($index = 0; $index -lt $count; $index++) {
        $begin = [int64]$offsets[$index]
        $end = if ($index + 1 -lt $count) { [int64]$offsets[$index + 1] } else { [int64]$bytes.Length }
        $length = $end - $begin
        if ($length -lt 24 -or (($length - 24) % 12) -ne 0) { throw "Invalid SOP operation length #$index" }

        $type = [BitConverter]::ToUInt32($bytes, [int]$begin)
        $metadata = [BitConverter]::ToUInt32($bytes, [int]$begin + 4)
        $targetKey = [BitConverter]::ToUInt32($bytes, [int]$begin + 8)
        $target = [BitConverter]::ToUInt32($bytes, [int]$begin + 12)
        $sourceKey = [BitConverter]::ToUInt32($bytes, [int]$begin + 16)
        $source = [BitConverter]::ToUInt32($bytes, [int]$begin + 20)
        if ($targetKey -ne 0x5B0292DD -or $sourceKey -ne 0x1B5B0525) { throw "Invalid SOP target/source fields #$index" }

        $propertyCount = ($metadata -shr 16) -band 0xff
        if ($propertyCount -ne (($length - 24) / 12)) { throw "SOP property count mismatch #$index" }
        $properties = [System.Collections.Generic.List[PSCustomObject]]::new()
        for ($propertyIndex = 0; $propertyIndex -lt $propertyCount; $propertyIndex++) {
            $offset = [int]$begin + 24 + 12 * $propertyIndex
            $propertyHash = [BitConverter]::ToUInt32($bytes, $offset)
            $propertyType = [BitConverter]::ToUInt32($bytes, $offset + 4)
            $rawValue = [BitConverter]::ToUInt32($bytes, $offset + 8)
            if ($propertyType -gt 1) { throw "Unsupported SOP property type #$index/$propertyIndex" }
            $value = if ($propertyType -eq 1) { [BitConverter]::ToSingle($bytes, $offset + 8) } else { $rawValue }
            $properties.Add([PSCustomObject]@{
                Hash = "0x{0:X8}" -f $propertyHash
                Type = if ($propertyType -eq 1) { "float" } else { "integer" }
                RawValue = "0x{0:X8}" -f $rawValue
                Value = $value
            })
        }

        $hash = "0x{0:X8}" -f $type
        $catalogKey = $hash.ToLowerInvariant()
        $known = $Catalog.ContainsKey($catalogKey)
        $info = if ($known) { $Catalog[$catalogKey] } else { $null }
        $operations.Add([PSCustomObject]@{
            Index = $index
            TargetBone = Format-SopBoneCode $target
            SourceBone = Format-SopBoneCode $source
            TypeHash = $hash
            Name = if ($known) { [string]$info.Name } else { "Unknown operation" }
            Category = if ($known) { [string]$info.Category } else { "Unknown" }
            Discovery = if ($known) { [string]$info.Discovery } else { "unknown" }
            DiscoveryLabel = if ($known) { [string]$info.DiscoveryLabel } else { "Not investigated" }
            Runtime = if ($known) { [string]$info.Runtime } else { "not_implemented" }
            RuntimeLabel = if ($known) { [string]$info.RuntimeLabel } else { "Not implemented" }
            Purpose = if ($known) { [string]$info.Purpose } else { "No catalog entry; raw properties are preserved for future analysis." }
            Metadata = "0x{0:X8}" -f $metadata
            PropertyCount = $propertyCount
            Properties = @($properties)
        })
    }

    return [PSCustomObject]@{
        File = [IO.Path]::GetFileName($Path)
        Version = "0x{0:X8}" -f $version
        OperationCount = $operations.Count
        ConfirmedCount = @($operations | Where-Object { $_.Discovery -in @('confirmed','core_confirmed') }).Count
        PartialCount = @($operations | Where-Object { $_.Discovery -eq 'partial' }).Count
        UnknownCount = @($operations | Where-Object { $_.Discovery -eq 'unknown' }).Count
        RuntimeSupportedCount = @($operations | Where-Object { $_.Runtime -eq 'implemented_guarded' }).Count
        Operations = @($operations)
    }
}
