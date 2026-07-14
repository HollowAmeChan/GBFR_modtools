# fix-encoding.ps1 - adds UTF-8 BOM to all .ps1 in this folder
# pure ASCII, no Chinese - safe to run without BOM itself
$dir = Split-Path $MyInvocation.MyCommand.Path -Resolve
Get-ChildItem $dir -Filter '*.ps1' | Where-Object { $_.Name -ne 'fix-encoding.ps1' } | ForEach-Object {
    $bytes = [IO.File]::ReadAllBytes($_.FullName)
    # skip if BOM already present (EF BB BF)
    if ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) {
        Write-Host "already has BOM: $($_.Name)"
        return
    }
    $text = [IO.File]::ReadAllText($_.FullName, [Text.Encoding]::UTF8)
    $enc  = New-Object System.Text.UTF8Encoding $true
    [IO.File]::WriteAllText($_.FullName, $text, $enc)
    Write-Host "BOM added: $($_.Name)"
}
Write-Host "done."
