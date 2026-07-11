# _step2_extract_tex.ps1
# 输入：.gts 文件路径（拖拽传入）
# 读取：output_hashes\*_hashes.txt（由第一步生成）
# 输出：output_textures\<角色名>\（PNG/DDS，按层编号区分）
#
# 调用：GraniteTextureReader.exe extract -t <gts> -f <哈希> -l -1 -o <输出目录>

param([string]$GtsPath)

$scriptDir  = Split-Path $PSCommandPath -Parent
$graniteExe = Join-Path $scriptDir "GraniteTextureReader.exe"
$hashesDir  = Join-Path $scriptDir "output_hashes"

# --- 校验 GraniteTextureReader ---
if (-not (Test-Path $graniteExe)) {
    Write-Host ""
    Write-Host " [错误] 未找到 GraniteTextureReader.exe："
    Write-Host "   $graniteExe"
    Write-Host ""
    Write-Host " 请从以下地址下载后放入工具包根目录："
    Write-Host "   https://github.com/Nenkai/GraniteTextureReader/releases"
    exit 1
}

# --- 校验 gts 文件 ---
if (-not $GtsPath -or -not (Test-Path $GtsPath)) {
    Write-Host " [错误] gts 文件不存在：$GtsPath"
    exit 1
}

Write-Host ""
Write-Host " gts 文件 : $GtsPath"

# --- 查找哈希表文件 ---
$hashFiles = Get-ChildItem "$hashesDir\*_hashes.txt" -ErrorAction SilentlyContinue |
             Sort-Object LastWriteTime -Descending

if ($hashFiles.Count -eq 0) {
    Write-Host ""
    Write-Host " [错误] 未找到哈希表文件："
    Write-Host "   $hashesDir"
    Write-Host ""
    Write-Host " 请先运行第一步：将 .minfo 文件拖拽到 01_minfo转Hash表.bat"
    exit 1
}

# 有多个哈希表时由用户选择
if ($hashFiles.Count -gt 1) {
    Write-Host ""
    Write-Host " 找到多个哈希表文件，请选择："
    for ($i = 0; $i -lt $hashFiles.Count; $i++) {
        $age = (Get-Date) - $hashFiles[$i].LastWriteTime
        $tag = if ($i -eq 0) { " <- 最新" } else { "" }
        $ageStr = if ($age.TotalHours -lt 1)   { "$([int]$age.TotalMinutes) 分钟前" }
                  elseif ($age.TotalDays -lt 1) { "$([int]$age.TotalHours) 小时前" }
                  else                          { "$([int]$age.TotalDays) 天前" }
        Write-Host ("   [{0}] {1,-40} ({2}){3}" -f $i, $hashFiles[$i].Name, $ageStr, $tag)
    }
    Write-Host ""
    $raw = Read-Host " 输入编号（直接回车使用最新 [0]）"
    $idx = if ($raw -match '^\d+$' -and [int]$raw -lt $hashFiles.Count) { [int]$raw } else { 0 }
    $hashFile = $hashFiles[$idx]
} else {
    $hashFile = $hashFiles[0]
}

$charName = $hashFile.BaseName -replace '_hashes$', ''
Write-Host " 哈希表   : $($hashFile.Name)（角色：$charName）"

# --- 读取哈希条目（跳过注释行） ---
$entries = Get-Content $hashFile |
           Where-Object { $_ -notmatch '^\s*#' -and $_ -match '[0-9a-f]{64}' } |
           ForEach-Object {
               $parts = $_ -split '\s+'
               [PSCustomObject]@{ Hash = $parts[0]; Name = $parts[1] }
           }

if ($entries.Count -eq 0) {
    Write-Host " [错误] 哈希表为空或格式错误：$($hashFile.FullName)"
    exit 1
}

Write-Host " 条目数   : $($entries.Count)"

# --- 准备输出目录 ---
$outDir = Join-Path $scriptDir "output_textures\$charName"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
Write-Host " 输出目录 : $outDir"
Write-Host ""

# --- 批量提取 ---
$success = 0
$fail    = 0
$total   = $entries.Count
$tmpErr  = Join-Path $env:TEMP "gtr_err_tmp.txt"

foreach ($entry in $entries) {
    $idx = $success + $fail + 1
    Write-Progress -Activity "正在提取纹理" `
                   -Status ("[{0}/{1}] {2}" -f $idx, $total, $entry.Name) `
                   -PercentComplete ([int](($idx / $total) * 100))

    $proc = Start-Process -FilePath $graniteExe `
        -ArgumentList @("extract",
                        "-t", $GtsPath,
                        "-f", $entry.Hash,
                        "-l", "-1",
                        "-o", $outDir) `
        -Wait -PassThru -NoNewWindow `
        -RedirectStandardError $tmpErr

    if ($proc.ExitCode -eq 0) {
        $success++
        Write-Host ("  OK  [{0}/{1}]  {2}" -f $idx, $total, $entry.Name) -ForegroundColor Green
    } else {
        $fail++
        $err = (Get-Content $tmpErr -Raw -ErrorAction SilentlyContinue) -replace "`n", " "
        Write-Host ("  !!  [{0}/{1}]  {2}  =>  {3}" -f $idx, $total, $entry.Name, $err) -ForegroundColor Yellow
    }
}

Write-Progress -Activity "正在提取纹理" -Completed

Write-Host ""
if ($fail -eq 0) {
    Write-Host (" 全部完成！共提取 {0} 个纹理。" -f $success) -ForegroundColor Green
} else {
    Write-Host (" 完成。成功：{0}，失败：{1}" -f $success, $fail) -ForegroundColor Yellow
    Write-Host " 失败的条目可能不在此 .gts 文件中。"
    Write-Host " 可尝试其他 gts 文件：granite\2k\gts\0\、granite\4k\gts\ 等。"
}
Write-Host ""
Write-Host " 输出目录："
Write-Host "   $outDir"
