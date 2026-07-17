param([switch]$Force)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$downloadRoot = Join-Path $repoRoot ".deps\downloads"
$toolRoot = Join-Path $repoRoot "_lib\tools"
New-Item -ItemType Directory -Force -Path $downloadRoot, $toolRoot | Out-Null

function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Test-ExpectedFile([string]$Path, [string]$Sha256) {
    return (Test-Path -LiteralPath $Path -PathType Leaf) -and
        ((Get-Sha256 $Path) -eq $Sha256.ToLowerInvariant())
}

function Get-VerifiedDownload(
    [string]$Name,
    [string]$Url,
    [string]$Sha256
) {
    $path = Join-Path $downloadRoot $Name
    if (-not $Force -and (Test-ExpectedFile $path $Sha256)) { return $path }
    if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force }
    Write-Host "[GET] $Name"
    try {
        Invoke-WebRequest -UseBasicParsing -Uri $Url -OutFile $path
    } catch {
        if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force }
        throw "Download failed: $Url`n$($_.Exception.Message)"
    }
    $actual = Get-Sha256 $path
    if ($actual -ne $Sha256.ToLowerInvariant()) {
        Remove-Item -LiteralPath $path -Force
        throw "SHA-256 mismatch for $Name. Expected $Sha256, got $actual"
    }
    return $path
}

function Install-DirectTool(
    [string]$Name,
    [string]$Version,
    [string]$Url,
    [string]$DownloadSha256,
    [string]$ExecutableSha256
) {
    $target = Join-Path $toolRoot $Name
    if (-not $Force -and (Test-ExpectedFile $target $ExecutableSha256)) {
        Write-Host "[OK] $Name $Version"
        return
    }
    $download = Get-VerifiedDownload "$Name-$Version.download" $Url $DownloadSha256
    $temporary = "$target.tmp"
    Copy-Item -LiteralPath $download -Destination $temporary -Force
    if (-not (Test-ExpectedFile $temporary $ExecutableSha256)) { throw "Installed executable hash mismatch: $Name" }
    Move-Item -LiteralPath $temporary -Destination $target -Force
    Unblock-File -LiteralPath $target -ErrorAction SilentlyContinue
    Write-Host "[OK] $Name $Version"
}

function Install-ZipExecutable(
    [string]$Name,
    [string]$Version,
    [string]$Url,
    [string]$ArchiveSha256,
    [string]$ExecutableSha256,
    [string]$ArchiveExecutable
) {
    $target = Join-Path $toolRoot $Name
    if (-not $Force -and (Test-ExpectedFile $target $ExecutableSha256)) {
        Write-Host "[OK] $Name $Version"
        return
    }
    $archive = Get-VerifiedDownload "$Name-$Version.zip" $Url $ArchiveSha256
    $staging = Join-Path ([IO.Path]::GetTempPath()) ("gbfr_tools_" + [Guid]::NewGuid().ToString("N"))
    try {
        Expand-Archive -LiteralPath $archive -DestinationPath $staging
        $source = Join-Path $staging $ArchiveExecutable
        if (-not (Test-ExpectedFile $source $ExecutableSha256)) { throw "Archive executable hash mismatch: $Name" }
        Copy-Item -LiteralPath $source -Destination "$target.tmp" -Force
        Move-Item -LiteralPath "$target.tmp" -Destination $target -Force
        Unblock-File -LiteralPath $target -ErrorAction SilentlyContinue
    } finally {
        if (Test-Path -LiteralPath $staging) { Remove-Item -LiteralPath $staging -Recurse -Force }
    }
    Write-Host "[OK] $Name $Version"
}

function Install-GbfrDataTools {
    $version = "2.0.0"
    $targetDir = Join-Path $toolRoot "GBFRDataTools"
    $targetExe = Join-Path $targetDir "GBFRDataTools.exe"
    $exeSha = "8693668fc4e35dcc44ec54d15b9ad1756a5e9a4e4c5e838a32c9245ee5e83257"
    if (-not $Force -and (Test-ExpectedFile $targetExe $exeSha)) {
        Write-Host "[OK] GBFRDataTools $version"
        return
    }
    $archive = Get-VerifiedDownload "GBFRDataTools-$version-win-x64.zip" `
        "https://github.com/Nenkai/GBFRDataTools/releases/download/2.0.0/GBFRDataTools-2.0.0-win-x64.zip" `
        "2f355e7785d7ed7d1a4f99b1fccc626bb9d949ce29a4f08b816a233dab77f63b"
    $staging = Join-Path ([IO.Path]::GetTempPath()) ("gbfr_datatools_" + [Guid]::NewGuid().ToString("N"))
    try {
        Expand-Archive -LiteralPath $archive -DestinationPath $staging
        $sourceDir = Join-Path $staging "win-x64"
        $sourceExe = Join-Path $sourceDir "GBFRDataTools.exe"
        if (-not (Test-ExpectedFile $sourceExe $exeSha)) { throw "GBFRDataTools executable hash mismatch" }
        if (Test-Path -LiteralPath $targetDir) { Remove-Item -LiteralPath $targetDir -Recurse -Force }
        Move-Item -LiteralPath $sourceDir -Destination $targetDir
        Get-ChildItem -LiteralPath $targetDir -Recurse -File | Unblock-File -ErrorAction SilentlyContinue
    } finally {
        if (Test-Path -LiteralPath $staging) { Remove-Item -LiteralPath $staging -Recurse -Force }
    }
    Write-Host "[OK] GBFRDataTools $version"
}

Install-GbfrDataTools
Install-ZipExecutable "flatc.exe" "25.12.19" `
    "https://github.com/google/flatbuffers/releases/download/v25.12.19-2026-02-06-03fffb2/Windows.flatc.binary.zip" `
    "68d51916873a3dbdaf7997ddfbbbfd6472b5907ffc62ccc9a88d146bbc0db87d" `
    "d873c85d970314177574b9bb468daea2084ae6198232a7045207bff2abd69aec" `
    "flatc.exe"
Install-DirectTool "GraniteTextureReader.exe" "1.1.5" `
    "https://github.com/Nenkai/GraniteTextureReader/releases/download/1.1.5/GraniteTextureReader.exe" `
    "b1f036bef11d86e42a4701b726f20b7d1456312debae96780228c701a3c62d42" `
    "b1f036bef11d86e42a4701b726f20b7d1456312debae96780228c701a3c62d42"
Install-DirectTool "texconv.exe" "may2026" `
    "https://github.com/microsoft/DirectXTex/releases/download/may2026/texconv.exe" `
    "dcfdec10244e02cf5037fba089c55fb7e1326b1c8181742d77d15fa5cb5eef06" `
    "dcfdec10244e02cf5037fba089c55fb7e1326b1c8181742d77d15fa5cb5eef06"

$versions = [ordered]@{
    GBFRDataTools = "2.0.0"
    FlatBuffers = "25.12.19"
    GraniteTextureReader = "1.1.5"
    DirectXTex = "may2026"
}
[IO.File]::WriteAllText((Join-Path $toolRoot "versions.json"), ($versions | ConvertTo-Json), [Text.UTF8Encoding]::new($false))
Write-Host "[OK] Runtime tools are ready in $toolRoot"
