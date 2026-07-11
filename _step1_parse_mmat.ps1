# _step1_parse_mmat.ps1
# 输入：.minfo 文件路径（拖拽传入）
# 输出：output_hashes\<角色名>_hashes.txt
#
# 原理：
#   mmat 是 FlatBuffers 二进制格式，但字符串字段以 ASCII 明文存储。
#   文件中的 64 位小写十六进制字符串就是 Granite 瓦片哈希，
#   与 gts/N/N_<哈希>.gtp 文件名以及 GraniteTextureReader -f 参数完全一致。
#   无需 flatc，无需任何额外解析工具。

param([string]$MinfoPath)

$scriptDir = Split-Path $PSCommandPath -Parent

# --- 校验输入 ---
if (-not $MinfoPath -or -not (Test-Path $MinfoPath)) {
    Write-Host " [错误] 文件不存在：$MinfoPath"
    exit 1
}

$minfoDir = Split-Path $MinfoPath -Parent
$charName = (Split-Path $MinfoPath -Leaf) -replace '\.minfo$', ''
$varsDir  = Join-Path $minfoDir "vars"

Write-Host " 角色名  : $charName"
Write-Host " minfo   : $MinfoPath"
Write-Host " vars 目录: $varsDir"

if (-not (Test-Path $varsDir)) {
    Write-Host ""
    Write-Host " [错误] 未找到 minfo 文件同级的 vars 目录。"
    Write-Host "        期望路径：$varsDir"
    Write-Host ""
    Write-Host " 请确认解包目录结构完整："
    Write-Host "   <角色名>.minfo"
    Write-Host "   vars\"
    Write-Host "     0.mmat, 1.mmat, ..."
    exit 1
}

# --- 查找 mmat 文件 ---
$mmatFiles = Get-ChildItem "$varsDir\*.mmat" -ErrorAction SilentlyContinue |
             Sort-Object { [int]($_.BaseName) }

if ($mmatFiles.Count -eq 0) {
    Write-Host " [错误] 在以下目录中未找到 .mmat 文件：$varsDir"
    exit 1
}

Write-Host " 找到     : $($mmatFiles.Count) 个 mmat 文件"
Write-Host ""

# --- 从 mmat 二进制提取 Granite 哈希及纹理名 ---
# FlatBuffers 字符串字段以原始 ASCII 存储，64 位小写十六进制即为 Granite 瓦片哈希
$entries  = [System.Collections.Generic.List[PSObject]]::new()
$seenHash = [System.Collections.Generic.HashSet[string]]::new()
$nameRx   = [regex]'([a-z][a-z0-9_]{3,60})'
$hashRx   = [regex]'[0-9a-f]{64}'

foreach ($mmat in $mmatFiles) {
    $bytes = [System.IO.File]::ReadAllBytes($mmat.FullName)
    $text  = [System.Text.Encoding]::ASCII.GetString($bytes)

    foreach ($hm in $hashRx.Matches($text)) {
        $h = $hm.Value
        if ($seenHash.Contains($h)) { continue }
        $seenHash.Add($h) | Out-Null

        # 哈希字节之后紧跟的 ASCII 字符串即为纹理名
        $after = $text.Substring($hm.Index + 64,
                     [Math]::Min(120, $text.Length - $hm.Index - 64))
        $nm    = $nameRx.Match($after)
        $name  = if ($nm.Success) { $nm.Value } else { "slot_$($h.Substring(0,8))" }

        $entries.Add([PSCustomObject]@{
            Hash   = $h
            Name   = $name
            Source = $mmat.Name
        })
    }
}

Write-Host " 唯一 Granite 哈希数量：$($entries.Count)"

# --- 写入输出文件 ---
$outDir  = Join-Path $scriptDir "output_hashes"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$outFile = Join-Path $outDir "${charName}_hashes.txt"

$lines = @()
$lines += "# GBFR Granite 纹理哈希表"
$lines += "# 角色名    : $charName"
$lines += "# 生成时间  : $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
$lines += "# 来源 minfo: $MinfoPath"
$lines += "# 共计      : $($entries.Count) 条"
$lines += "# 用法      : GraniteTextureReader.exe extract -t <.gts> -f <HASH> -l -1"
$lines += "#"
$lines += "# HASH                                                            NAME                                      [MMAT]"
$lines += "# ---------------------------------------------------------------- ----------------------------------------- ------"

foreach ($e in $entries) {
    $lines += ("{0}  {1,-40}  [{2}]" -f $e.Hash, $e.Name, $e.Source)
}

$lines | Set-Content $outFile -Encoding ASCII

Write-Host ""
Write-Host " 哈希表已保存至："
Write-Host "   $outFile"
Write-Host ""
Write-Host " 预览（前 5 条）："
$entries | Select-Object -First 5 |
    Format-Table -AutoSize Name, Source,
        @{Label="Hash（前16位）"; Expression={$_.Hash.Substring(0,16) + "..."}}
