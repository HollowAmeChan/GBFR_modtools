# GBFR_Extractor.ps1  —  multi-minfo x multi-gts GUI extractor + WTB packer + mmat editor
# Drop multiple .minfo and .gts files into their lists, then Extract.
# Output is organised as: output_textures/<charId>/<gts_stem>/

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$scriptDir  = Split-Path $PSCommandPath -Parent
$graniteExe = Join-Path $scriptDir "GraniteTextureReader.exe"
$flatcExe   = Join-Path $scriptDir "flatc.exe"
$schemaFbs  = Join-Path $scriptDir "MMat_ModelMaterial.fbs"
$nierDir    = Get-ChildItem $scriptDir -Directory -Filter "nier_cli_mgrr_*" |
              Select-Object -First 1 -ExpandProperty FullName
$nierExe    = if ($nierDir) { Join-Path $nierDir "nier_cli_mgrr.exe" } else { $null }
$hashesDir  = Join-Path $scriptDir "output_hashes"
$outputDir  = Join-Path $scriptDir "output_textures"
$outputDds  = Join-Path $scriptDir "output_dds"
$outputMod  = Join-Path $scriptDir "output_mod"

$script:Working   = $false
$script:LogQueue  = [System.Collections.Concurrent.ConcurrentQueue[string]]::new()
$script:ProgQueue = [System.Collections.Concurrent.ConcurrentQueue[int]]::new()

# ── palette ──────────────────────────────────────────────────────────────────
$C = @{
    Bg      = [Drawing.Color]::FromArgb(30,30,30)
    Panel   = [Drawing.Color]::FromArgb(45,45,48)
    PanelOk = [Drawing.Color]::FromArgb(30,65,38)
    Accent  = [Drawing.Color]::FromArgb(0,120,215)
    Warn    = [Drawing.Color]::FromArgb(200,120,0)
    Text    = [Drawing.Color]::White
    Sub     = [Drawing.Color]::FromArgb(160,160,160)
    LogBg   = [Drawing.Color]::FromArgb(18,18,18)
}
$fUI   = New-Object Drawing.Font("Segoe UI", 10)
$fMono = New-Object Drawing.Font("Consolas",  9)
$fBig  = New-Object Drawing.Font("Segoe UI", 11, [Drawing.FontStyle]::Bold)

# ── root form ────────────────────────────────────────────────────────────────
$form = New-Object Windows.Forms.Form
$form.Text          = "GBFR Modtools"
$form.ClientSize    = New-Object Drawing.Size(780, 620)
$form.MinimumSize   = New-Object Drawing.Size(640, 520)
$form.BackColor     = $C.Bg
$form.ForeColor     = $C.Text
$form.Font          = $fUI
$form.StartPosition = "CenterScreen"

# ── tab control ──────────────────────────────────────────────────────────────
$tabs = New-Object Windows.Forms.TabControl
$tabs.Dock         = "Fill"
$tabs.BackColor    = $C.Bg
$tabs.Font         = $fUI

$tabEx   = New-Object Windows.Forms.TabPage; $tabEx.Text   = "  Extract  "; $tabEx.BackColor = $C.Bg
$tabPack = New-Object Windows.Forms.TabPage; $tabPack.Text = "  Pack     "; $tabPack.BackColor = $C.Bg
$tabs.Controls.AddRange(@($tabEx, $tabPack))
$form.Controls.Add($tabs)

# --------------------------------------------------------------------------
#  EXTRACT TAB
# --------------------------------------------------------------------------
function MakeListPanel($parent, [int]$x, [int]$y, [int]$w, [int]$h, [string]$ext, [string]$hint) {
    $w12 = $w - 12
    $h60 = $h - 60
    $h26 = $h - 26
    $p = New-Object Windows.Forms.Panel
    $p.Location   = New-Object Drawing.Point($x, $y)
    $p.Size       = New-Object Drawing.Size($w, $h)
    $p.BackColor  = $C.Panel
    $p.AllowDrop  = $true

    $lbl = New-Object Windows.Forms.Label
    $lbl.Text      = $hint
    $lbl.Location  = New-Object Drawing.Point(6, 5)
    $lbl.Size      = New-Object Drawing.Size($w12, 22)
    $lbl.ForeColor = $C.Sub
    $lbl.Font      = $fUI

    $box = New-Object Windows.Forms.ListBox
    $box.Location         = New-Object Drawing.Point(6, 30)
    $box.Size             = New-Object Drawing.Size($w12, $h60)
    $box.BackColor        = [Drawing.Color]::FromArgb(28,28,28)
    $box.ForeColor        = $C.Text
    $box.Font             = $fMono
    $box.SelectionMode    = "MultiExtended"
    $box.BorderStyle      = "None"
    $box.AllowDrop        = $true

    $btnAdd = New-Object Windows.Forms.Button
    $btnAdd.Text      = "+ Add"
    $btnAdd.Location  = New-Object Drawing.Point(6, $h26)
    $btnAdd.Size      = New-Object Drawing.Size(70, 22)
    $btnAdd.FlatStyle = "Flat"
    $btnAdd.BackColor = $C.Panel
    $btnAdd.ForeColor = $C.Sub
    $btnAdd.Font      = $fUI

    $btnDel = New-Object Windows.Forms.Button
    $btnDel.Text      = "- Remove"
    $btnDel.Location  = New-Object Drawing.Point(82, $h26)
    $btnDel.Size      = New-Object Drawing.Size(80, 22)
    $btnDel.FlatStyle = "Flat"
    $btnDel.BackColor = $C.Panel
    $btnDel.ForeColor = $C.Sub
    $btnDel.Font      = $fUI

    $btnCl = New-Object Windows.Forms.Button
    $btnCl.Text      = "Clear"
    $btnCl.Location  = New-Object Drawing.Point(168, $h26)
    $btnCl.Size      = New-Object Drawing.Size(56, 22)
    $btnCl.FlatStyle = "Flat"
    $btnCl.BackColor = $C.Panel
    $btnCl.ForeColor = $C.Sub
    $btnCl.Font      = $fUI

    $p.Controls.AddRange(@($lbl, $box, $btnAdd, $btnDel, $btnCl))
    $parent.Controls.Add($p)

    # drag-drop onto panel or listbox
    $dropHandler = {
        $files = $_.Data.GetData([Windows.Forms.DataFormats]::FileDrop)
        foreach ($f in $files) {
            if ([IO.Path]::GetExtension($f) -ieq $ext -and $box.Items.IndexOf($f) -lt 0) {
                $box.Items.Add($f) | Out-Null
            }
        }
        $p.BackColor = $C.PanelOk
        UpdateExtractBtn
    }.GetNewClosure()
    $p.Add_DragEnter(({ if ($_.Data.GetDataPresent([Windows.Forms.DataFormats]::FileDrop)) { $_.Effect = "Copy" } }).GetNewClosure())
    $p.Add_DragDrop($dropHandler)
    $box.Add_DragEnter(({ if ($_.Data.GetDataPresent([Windows.Forms.DataFormats]::FileDrop)) { $_.Effect = "Copy" } }).GetNewClosure())
    $box.Add_DragDrop($dropHandler)

    # browse
    $btnAdd.Add_Click(({
        $d = New-Object Windows.Forms.OpenFileDialog
        $d.Filter    = "$ext files (*$ext)|*$ext"
        $d.Multiselect = $true
        if ($d.ShowDialog() -eq "OK") {
            foreach ($f in $d.FileNames) {
                if ($box.Items.IndexOf($f) -lt 0) { $box.Items.Add($f) | Out-Null }
            }
            if ($box.Items.Count -gt 0) { $p.BackColor = $C.PanelOk }
            UpdateExtractBtn
        }
    }).GetNewClosure())

    $btnDel.Add_Click(({
        $sel = @($box.SelectedItems)
        foreach ($s in $sel) { $box.Items.Remove($s) }
        if ($box.Items.Count -eq 0) { $p.BackColor = $C.Panel }
        UpdateExtractBtn
    }).GetNewClosure())

    $btnCl.Add_Click(({
        $box.Items.Clear()
        $p.BackColor = $C.Panel
        UpdateExtractBtn
    }).GetNewClosure())

    return $box
}

# list panels
$boxMinfo = MakeListPanel $tabEx  10 10 370 170 ".minfo" "minfo files  (drag & drop or Add)"
$boxGts   = MakeListPanel $tabEx 390 10 370 170 ".gts"   "gts files    (drag & drop or Add)"

# extract / clear buttons
$btnEx = New-Object Windows.Forms.Button
$btnEx.Text      = "Extract"
$btnEx.Location  = New-Object Drawing.Point(10, 188)
$btnEx.Size      = New-Object Drawing.Size(110, 32)
$btnEx.BackColor = $C.Accent
$btnEx.ForeColor = $C.Text
$btnEx.FlatStyle = "Flat"
$btnEx.Enabled   = $false
$tabEx.Controls.Add($btnEx)

$btnClrLog = New-Object Windows.Forms.Button
$btnClrLog.Text      = "Clear log"
$btnClrLog.Location  = New-Object Drawing.Point(130, 188)
$btnClrLog.Size      = New-Object Drawing.Size(80, 32)
$btnClrLog.BackColor = $C.Panel
$btnClrLog.ForeColor = $C.Sub
$btnClrLog.FlatStyle = "Flat"
$tabEx.Controls.Add($btnClrLog)

$btnOpenOut = New-Object Windows.Forms.Button
$btnOpenOut.Text      = "Open output"
$btnOpenOut.Location  = New-Object Drawing.Point(220, 188)
$btnOpenOut.Size      = New-Object Drawing.Size(100, 32)
$btnOpenOut.BackColor = $C.Panel
$btnOpenOut.ForeColor = $C.Sub
$btnOpenOut.FlatStyle = "Flat"
$tabEx.Controls.Add($btnOpenOut)

# progress
$prog = New-Object Windows.Forms.ProgressBar
$prog.Location = New-Object Drawing.Point(10, 227)
$prog.Size     = New-Object Drawing.Size(750, 10)
$prog.Minimum  = 0; $prog.Maximum = 100
$tabEx.Controls.Add($prog)

# log
$txtLog = New-Object Windows.Forms.TextBox
$txtLog.Multiline   = $true
$txtLog.ScrollBars  = "Vertical"
$txtLog.ReadOnly    = $true
$txtLog.Location    = New-Object Drawing.Point(10, 243)
$txtLog.Size        = New-Object Drawing.Size(750, 330)
$txtLog.Font        = $fMono
$txtLog.BackColor   = $C.LogBg
$txtLog.ForeColor   = $C.Text
$txtLog.BorderStyle = "None"
$tabEx.Controls.Add($txtLog)

# resize handler
$tabEx.Add_Resize({
    $w = $tabEx.ClientSize.Width; $h = $tabEx.ClientSize.Height
    $half = [int](($w - 30) / 2)
    # reposition panels (find them by type)
    $panels = $tabEx.Controls | Where-Object { $_ -is [Windows.Forms.Panel] }
    if ($panels.Count -ge 2) {
        $panels[0].Width = $half
        $panels[1].Width = $half
        $panels[1].Left  = $panels[0].Right + 10
        # resize listboxes inside
        foreach ($pp in $panels) {
            $lb = $pp.Controls | Where-Object { $_ -is [Windows.Forms.ListBox] }
            if ($lb) { $lb.Width = $pp.Width - 12 }
        }
    }
    $prog.Width    = $w - 20
    $txtLog.Width  = $w - 20
    $txtLog.Height = $h - 253
})

function UpdateExtractBtn {
    $btnEx.Enabled = ($boxMinfo.Items.Count -gt 0) -and
                     ($boxGts.Items.Count   -gt 0) -and
                     (-not $script:Working)
}

function AppendLog($msg) {
    if ($txtLog.InvokeRequired) {
        $txtLog.Invoke([Action[string]]{ param($m) AppendLog $m }, $msg)
        return
    }
    $txtLog.AppendText($msg + "`r`n")
    $txtLog.SelectionStart = $txtLog.TextLength
    $txtLog.ScrollToCaret()
}

$btnClrLog.Add_Click({ $txtLog.Clear() })
$btnOpenOut.Add_Click({ if (Test-Path $outputDir) { explorer.exe $outputDir } })

# ── extraction worker ─────────────────────────────────────────────────────────
$btnEx.Add_Click({
    if ($script:Working) { return }
    $script:Working = $true
    UpdateExtractBtn
    $txtLog.Clear(); $prog.Value = 0

    $minfoList = @($boxMinfo.Items)
    $gtsList   = @($boxGts.Items)

    $rs = [runspacefactory]::CreateRunspace()
    $rs.ApartmentState = "STA"; $rs.ThreadOptions = "ReuseThread"; $rs.Open()
    $rs.SessionStateProxy.SetVariable("MinfoList",   $minfoList)
    $rs.SessionStateProxy.SetVariable("GtsList",     $gtsList)
    $rs.SessionStateProxy.SetVariable("GraniteExe",  $graniteExe)
    $rs.SessionStateProxy.SetVariable("HashesDir",   $hashesDir)
    $rs.SessionStateProxy.SetVariable("OutputDir",   $outputDir)
    $rs.SessionStateProxy.SetVariable("LogQ",        $script:LogQueue)
    $rs.SessionStateProxy.SetVariable("ProgQ",       $script:ProgQueue)

    $ps = [powershell]::Create(); $ps.Runspace = $rs
    $ps.AddScript({
        function L($m) { $LogQ.Enqueue($m) }

        $nameRx = [regex]'([a-z][a-z0-9_]{3,60})'
        $hashRx = [regex]'[0-9a-f]{64}'

        # ── step 1: parse all minfo/mmat files ───────────────────────────────
        $allEntries = [System.Collections.Generic.List[PSObject]]::new()
        $seenHash   = [System.Collections.Generic.HashSet[string]]::new()

        foreach ($minfo in $MinfoList) {
            $charId   = [IO.Path]::GetFileNameWithoutExtension($minfo)
            $varsDir  = Join-Path (Split-Path $minfo -Parent) "vars"
            L "--- $charId ---"
            if (-not (Test-Path $varsDir)) { L "  [skip] no vars/ next to $minfo"; continue }

            $mmats = Get-ChildItem "$varsDir\*.mmat" -EA SilentlyContinue |
                     Sort-Object { [int]($_.BaseName) }
            L "  $($mmats.Count) mmat file(s)"

            foreach ($mmat in $mmats) {
                $text = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($mmat.FullName))
                foreach ($hm in $hashRx.Matches($text)) {
                    $h = $hm.Value
                    if ($seenHash.Contains($h)) { continue }
                    $seenHash.Add($h) | Out-Null
                    $after = $text.Substring($hm.Index+64, [Math]::Min(120,$text.Length-$hm.Index-64))
                    $nm    = $nameRx.Match($after)
                    $name  = if ($nm.Success) { $nm.Value } else { "slot_$($h.Substring(0,8))" }
                    $allEntries.Add([PSCustomObject]@{Hash=$h;Name=$name;CharId=$charId;Source=$mmat.Name})
                }
            }
            L "  running total: $($seenHash.Count) unique hashes"
        }

        L ""
        L "Total unique hashes: $($allEntries.Count)"

        # save merged hash table
        New-Item -ItemType Directory -Force -Path $HashesDir | Out-Null
        $stamp = Get-Date -Format "yyyyMMdd_HHmmss"
        $hashFile = Join-Path $HashesDir "merged_$stamp.txt"
        $lines = @("# GBFR merged hash table  $stamp")
        foreach ($e in $allEntries) {
            $lines += "$($e.Hash)  $($e.Name)  [$($e.CharId)/$($e.Source)]"
        }
        $lines | Set-Content $hashFile -Encoding ASCII
        L "Hash table -> $hashFile"

        # ── step 2: extract from each gts ────────────────────────────────────
        $gTotal   = $GtsList.Count * $allEntries.Count
        $gDone    = 0

        foreach ($gts in $GtsList) {
            $gtsStem = [IO.Path]::GetFileNameWithoutExtension($gts)
            $gtsDir  = [IO.Path]::GetFileName([IO.Path]::GetDirectoryName($gts))
            $label   = "${gtsDir}_${gtsStem}"   # e.g. "1_1" or "4k_0_0"
            L ""
            L "=== GTS: $gts ==="
            $ok = 0; $fail = 0
            $tmp = Join-Path $env:TEMP "gtr_err.txt"

            foreach ($entry in $allEntries) {
                $gDone++
                $outDir = Join-Path (Join-Path $OutputDir $entry.CharId) $label
                New-Item -ItemType Directory -Force -Path $outDir | Out-Null

                $proc = Start-Process -FilePath $GraniteExe `
                    -ArgumentList @("extract","-t","`"$gts`"","-f",$entry.Hash,"-l","-1","-o","`"$outDir`"") `
                    -Wait -PassThru -NoNewWindow -RedirectStandardError $tmp

                if ($proc.ExitCode -eq 0) {
                    # check if files actually landed (GraniteTextureReader exits 0 even on miss)
                    $landed = (Get-ChildItem $outDir -EA SilentlyContinue).Count
                    if ($landed -gt 0) {
                        $ok++
                        L "  OK   $($entry.Name)"
                    }
                    # silently skip if no files (hash not in this gts)
                } else {
                    $fail++
                    $err = (Get-Content $tmp -Raw -EA SilentlyContinue) -replace "`n"," "
                    L "  !!   $($entry.Name)  =>  $err"
                }
                $ProgQ.Enqueue([int](($gDone / $gTotal) * 100))
            }
            L ("  {0}: extracted {1}, not in this gts: {2}, failed: {3}" -f $gtsStem, $ok, ($allEntries.Count - $ok - $fail), $fail)
        }
        L ""; L "##DONE##"
    }) | Out-Null

    $handle = $ps.BeginInvoke()
    $timer  = New-Object Windows.Forms.Timer; $timer.Interval = 80
    $timer.Add_Tick({
        $msg = ""; $pct = 0
        while ($script:LogQueue.TryDequeue([ref]$msg)) { AppendLog $msg }
        while ($script:ProgQueue.TryDequeue([ref]$pct)) { $prog.Value = [Math]::Min($pct,100) }
        if ($handle.IsCompleted) {
            $timer.Stop()
            try { $ps.EndInvoke($handle) } catch {}
            $ps.Dispose(); $rs.Dispose()
            $script:Working = $false; $prog.Value = 100; UpdateExtractBtn
        }
    })
    $timer.Start()
})

# --------------------------------------------------------------------------
#  PACK TAB
# --------------------------------------------------------------------------

# -- WTB helper functions --------------------------------------------------

function Invoke-WtbExtract([string]$texPath, [string]$outDir) {
    # Read WTB, extract each non-empty DDS slot to <basename>_<i>.dds
    $b       = [IO.File]::ReadAllBytes($texPath)
    $count   = [BitConverter]::ToUInt32($b, 4)
    $offTbl  = [BitConverter]::ToUInt32($b, 12)
    $sizeTbl = [BitConverter]::ToUInt32($b, 16)
    $base    = [IO.Path]::GetFileNameWithoutExtension($texPath)
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null
    $extracted = 0
    for ($i = 0; $i -lt $count; $i++) {
        $off  = [BitConverter]::ToUInt32($b, $offTbl  + $i * 4)
        $size = [BitConverter]::ToUInt32($b, $sizeTbl + $i * 4)
        if ($off -eq 0 -or $size -eq 0) { continue }
        $outPath = Join-Path $outDir "${base}_${i}.dds"
        [IO.File]::WriteAllBytes($outPath, $b[$off..($off + $size - 1)])
        $extracted++
    }
    return $extracted
}

function Invoke-WtbPack([string]$originalTex, [string]$ddsPath, [string]$outDir) {
    # Replace slot-0 DDS in original WTB with new DDS, write to outDir
    $orig    = [IO.File]::ReadAllBytes($originalTex)
    $newDds  = [IO.File]::ReadAllBytes($ddsPath)
    $offTbl  = [BitConverter]::ToUInt32($orig, 12)   # = 32
    $sizeTbl = [BitConverter]::ToUInt32($orig, 16)   # = 64
    $ddsOff  = [BitConverter]::ToUInt32($orig, $offTbl)  # = 4096

    # Copy header section (everything before the DDS data), patch size
    $header = $orig[0..($ddsOff - 1)]
    $sizeBytes = [BitConverter]::GetBytes([uint32]$newDds.Length)
    [Array]::Copy($sizeBytes, 0, $header, $sizeTbl, 4)

    # Assemble: header + new DDS
    $output = New-Object byte[] ($header.Length + $newDds.Length)
    [Array]::Copy($header, 0, $output, 0, $header.Length)
    [Array]::Copy($newDds, 0, $output, $header.Length, $newDds.Length)

    New-Item -ItemType Directory -Force -Path $outDir | Out-Null
    $outPath = Join-Path $outDir ([IO.Path]::GetFileName($originalTex))
    [IO.File]::WriteAllBytes($outPath, $output)
    return $outPath
}

# -- Pack tab log box -------------------------------------------------------
$txtPackLog = New-Object Windows.Forms.TextBox
$txtPackLog.Multiline   = $true
$txtPackLog.ScrollBars  = "Vertical"
$txtPackLog.ReadOnly    = $true
$txtPackLog.Font        = $fMono
$txtPackLog.BackColor   = $C.LogBg
$txtPackLog.ForeColor   = $C.Text
$txtPackLog.BorderStyle = "None"

function PLog($msg) {
    if ($txtPackLog.InvokeRequired) {
        $txtPackLog.Invoke([Action[string]]{ param($m) PLog $m }, $msg)
        return
    }
    $txtPackLog.AppendText($msg + "`r`n")
    $txtPackLog.SelectionStart = $txtPackLog.TextLength
    $txtPackLog.ScrollToCaret()
}

# -- tool status banner ----------------------------------------------------
$pStatus = New-Object Windows.Forms.Panel
$pStatus.Location  = New-Object Drawing.Point(10, 8)
$pStatus.Size      = New-Object Drawing.Size(750, 22)
$pStatus.BackColor = $C.Bg
$tabPack.Controls.Add($pStatus)

function StatusLbl($text, $ok, $x) {
    $l = New-Object Windows.Forms.Label
    $l.Text      = $text
    $l.Location  = New-Object Drawing.Point($x, 2)
    $l.AutoSize  = $true
    $l.Font      = $fUI
    $l.ForeColor = if ($ok) { [Drawing.Color]::FromArgb(80,200,80) } else { $C.Warn }
    $pStatus.Controls.Add($l)
    return $l
}

$xCur = 0
StatusLbl ("flatc: " + $(if (Test-Path $flatcExe) {"OK"} else {"missing (download from github.com/google/flatbuffers)"})) `
    (Test-Path $flatcExe) $xCur | Out-Null
$xCur += 380
StatusLbl ("schema: " + $(if (Test-Path $schemaFbs) {"OK"} else {"missing"})) `
    (Test-Path $schemaFbs) $xCur | Out-Null

# ── SECTION 1: WTB Textures ────────────────────────────────────────────────
$lblWtb = New-Object Windows.Forms.Label
$lblWtb.Text     = "WTB Textures  (data/texture/2k/*.texture)"
$lblWtb.Font     = $fBig
$lblWtb.Location = New-Object Drawing.Point(10, 36)
$lblWtb.AutoSize = $true
$tabPack.Controls.Add($lblWtb)

# WTB left panel: original .texture files
$boxTexture = MakeListPanel $tabPack 10 62 360 130 ".texture" "Original .texture files  (from data/texture/2k/)"
$tabPack.Controls[-1].Location = New-Object Drawing.Point(10, 62)

# WTB right panel: edited DDS files
$boxDds = MakeListPanel $tabPack 390 62 370 130 ".dds" "Edited .dds files  (one per original)"
$tabPack.Controls[-1].Location = New-Object Drawing.Point(390, 62)

$btnWtbExtract = New-Object Windows.Forms.Button
$btnWtbExtract.Text      = "Extract DDS"
$btnWtbExtract.Location  = New-Object Drawing.Point(10, 200)
$btnWtbExtract.Size      = New-Object Drawing.Size(110, 28)
$btnWtbExtract.BackColor = $C.Panel
$btnWtbExtract.ForeColor = $C.Text
$btnWtbExtract.FlatStyle = "Flat"
$tabPack.Controls.Add($btnWtbExtract)

$btnWtbPack = New-Object Windows.Forms.Button
$btnWtbPack.Text      = "Pack to .texture"
$btnWtbPack.Location  = New-Object Drawing.Point(130, 200)
$btnWtbPack.Size      = New-Object Drawing.Size(130, 28)
$btnWtbPack.BackColor = $C.Accent
$btnWtbPack.ForeColor = $C.Text
$btnWtbPack.FlatStyle = "Flat"
$tabPack.Controls.Add($btnWtbPack)

$btnOpenDds = New-Object Windows.Forms.Button
$btnOpenDds.Text      = "Open DDS folder"
$btnOpenDds.Location  = New-Object Drawing.Point(270, 200)
$btnOpenDds.Size      = New-Object Drawing.Size(120, 28)
$btnOpenDds.BackColor = $C.Panel
$btnOpenDds.ForeColor = $C.Sub
$btnOpenDds.FlatStyle = "Flat"
$tabPack.Controls.Add($btnOpenDds)

$btnOpenMod = New-Object Windows.Forms.Button
$btnOpenMod.Text      = "Open output_mod"
$btnOpenMod.Location  = New-Object Drawing.Point(400, 200)
$btnOpenMod.Size      = New-Object Drawing.Size(120, 28)
$btnOpenMod.BackColor = $C.Panel
$btnOpenMod.ForeColor = $C.Sub
$btnOpenMod.FlatStyle = "Flat"
$tabPack.Controls.Add($btnOpenMod)

# ── SECTION 2: mmat Edit ──────────────────────────────────────────────────
$lblMmat = New-Object Windows.Forms.Label
$lblMmat.Text     = "mmat Edit  (flatc decode/encode)"
$lblMmat.Font     = $fBig
$lblMmat.Location = New-Object Drawing.Point(10, 238)
$lblMmat.AutoSize = $true
$tabPack.Controls.Add($lblMmat)

$boxMmat = MakeListPanel $tabPack 10 264 360 100 ".mmat" ".mmat files  (from model/pl/<char>/vars/)"
$tabPack.Controls[-1].Location = New-Object Drawing.Point(10, 264)

$boxMmatJson = MakeListPanel $tabPack 390 264 370 100 ".json" "Edited .json files  (to repack)"
$tabPack.Controls[-1].Location = New-Object Drawing.Point(390, 264)

$btnMmatDecode = New-Object Windows.Forms.Button
$btnMmatDecode.Text      = "Decode to JSON"
$btnMmatDecode.Location  = New-Object Drawing.Point(10, 372)
$btnMmatDecode.Size      = New-Object Drawing.Size(130, 28)
$btnMmatDecode.BackColor = $C.Panel
$btnMmatDecode.ForeColor = $C.Text
$btnMmatDecode.FlatStyle = "Flat"
$tabPack.Controls.Add($btnMmatDecode)

$btnMmatEncode = New-Object Windows.Forms.Button
$btnMmatEncode.Text      = "Encode to .mmat"
$btnMmatEncode.Location  = New-Object Drawing.Point(150, 372)
$btnMmatEncode.Size      = New-Object Drawing.Size(130, 28)
$btnMmatEncode.BackColor = $C.Accent
$btnMmatEncode.ForeColor = $C.Text
$btnMmatEncode.FlatStyle = "Flat"
$tabPack.Controls.Add($btnMmatEncode)

# pack log
$txtPackLog.Location = New-Object Drawing.Point(10, 408)
$txtPackLog.Size     = New-Object Drawing.Size(750, 152)
$tabPack.Controls.Add($txtPackLog)

# resize handler for Pack tab
$tabPack.Add_Resize({
    $w = $tabPack.ClientSize.Width
    $h = $tabPack.ClientSize.Height
    $half = [int](($w - 30) / 2)
    $panels = $tabPack.Controls | Where-Object { $_ -is [Windows.Forms.Panel] -and $_.Tag -ne 'status' }
    if ($panels.Count -ge 4) {
        $panels[0].Width = $half; $panels[1].Width = $w - $half - 30
        $panels[1].Left  = $panels[0].Right + 10
        $panels[2].Width = $half; $panels[3].Width = $w - $half - 30
        $panels[3].Left  = $panels[2].Right + 10
        foreach ($pp in $panels) {
            $lb = $pp.Controls | Where-Object { $_ -is [Windows.Forms.ListBox] }
            if ($lb) { $lb.Width = $pp.Width - 12 }
        }
    }
    $txtPackLog.Width  = $w - 20
    $txtPackLog.Height = $h - 418
})

# ── button handlers ────────────────────────────────────────────────────────

$btnWtbExtract.Add_Click({
    if ($boxTexture.Items.Count -eq 0) { PLog "[WARN] No .texture files in the list."; return }
    $outDir = $outputDds
    $total = 0
    foreach ($t in @($boxTexture.Items)) {
        $n = Invoke-WtbExtract $t $outDir
        PLog "Extracted $n DDS from: $([IO.Path]::GetFileName($t))"
        $total += $n
    }
    PLog "Done. $total DDS files -> $outDir"
}.GetNewClosure())

$btnOpenDds.Add_Click(({ if (Test-Path $outputDds) { explorer.exe $outputDds } else { PLog "output_dds/ does not exist yet." } }).GetNewClosure())
$btnOpenMod.Add_Click(({ if (Test-Path $outputMod) { explorer.exe $outputMod } else { PLog "output_mod/ does not exist yet." } }).GetNewClosure())

$btnWtbPack.Add_Click({
    if ($boxTexture.Items.Count -eq 0) { PLog "[WARN] No original .texture files."; return }
    if ($boxDds.Items.Count -eq 0)     { PLog "[WARN] No edited .dds files."; return }

    # Match DDS to .texture by base name:  pl1400_skin_lod0_msk2_0.dds → pl1400_skin_lod0_msk2.texture
    $outDir = Join-Path $outputMod "data\texture\2k"
    $ok = 0; $miss = 0

    foreach ($tex in @($boxTexture.Items)) {
        $baseTex = [IO.Path]::GetFileNameWithoutExtension($tex)
        # find matching DDS (name starts with baseTex)
        $match = @($boxDds.Items) | Where-Object { [IO.Path]::GetFileNameWithoutExtension($_) -match "^${baseTex}_\d+$" } | Select-Object -First 1
        if (-not $match) {
            PLog "  NO MATCH for $baseTex  (expected DDS named ${baseTex}_0.dds)"
            $miss++
            continue
        }
        $outPath = Invoke-WtbPack $tex $match $outDir
        PLog "  PACKED: $([IO.Path]::GetFileName($outPath))"
        $ok++
    }
    PLog "Done. Packed: $ok  Missing DDS: $miss"
    PLog "Output: $outDir"
}.GetNewClosure())

$btnMmatDecode.Add_Click({
    if ($boxMmat.Items.Count -eq 0) { PLog "[WARN] No .mmat files in the list."; return }
    if (-not (Test-Path $flatcExe)) {
        PLog "[ERROR] flatc.exe not found."
        PLog "        Download: https://github.com/google/flatbuffers/releases -> Windows.flatc.binary.zip"
        PLog "        Place flatc.exe next to this script."
        return
    }
    if (-not (Test-Path $schemaFbs)) { PLog "[ERROR] MMat_ModelMaterial.fbs not found."; return }

    $outDir = Join-Path $outputMod "mmat_json"
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null

    foreach ($mmat in @($boxMmat.Items)) {
        PLog "Decoding: $([IO.Path]::GetFileName($mmat)) ..."
        $result = & $flatcExe --json --strict-json --raw-binary $schemaFbs -- $mmat -o $outDir 2>&1
        if ($LASTEXITCODE -eq 0) {
            PLog "  -> OK"
        } else {
            PLog "  -> ERROR: $result"
        }
    }
    PLog "JSON files: $outDir"
    if (Test-Path $outDir) { explorer.exe $outDir }
}.GetNewClosure())

$btnMmatEncode.Add_Click({
    if ($boxMmatJson.Items.Count -eq 0) { PLog "[WARN] No .json files in the list."; return }
    if (-not (Test-Path $flatcExe)) { PLog "[ERROR] flatc.exe not found."; return }
    if (-not (Test-Path $schemaFbs)) { PLog "[ERROR] MMat_ModelMaterial.fbs not found."; return }

    $outDir = Join-Path $outputMod "mmat_repacked"
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null

    foreach ($json in @($boxMmatJson.Items)) {
        PLog "Encoding: $([IO.Path]::GetFileName($json)) ..."
        $result = & $flatcExe --binary $schemaFbs $json -o $outDir 2>&1
        if ($LASTEXITCODE -eq 0) {
            # flatc outputs <basename>.bin - rename to .mmat
            $binName  = [IO.Path]::GetFileNameWithoutExtension($json) + ".bin"
            $mmatName = [IO.Path]::GetFileNameWithoutExtension($json) + ".mmat"
            $binPath  = Join-Path $outDir $binName
            $mmatPath = Join-Path $outDir $mmatName
            if (Test-Path $binPath) { Move-Item $binPath $mmatPath -Force }
            PLog "  -> $mmatName"
        } else {
            PLog "  -> ERROR: $result"
        }
    }
    PLog "mmat files: $outDir"
}.GetNewClosure())

# ── startup message ───────────────────────────────────────────────────────────
AppendLog "GBFR Modtools ready."
AppendLog ""
if (-not (Test-Path $graniteExe)) {
    AppendLog "[WARN] GraniteTextureReader.exe not found in this folder."
    AppendLog "       https://github.com/Nenkai/GraniteTextureReader/releases"
    AppendLog ""
}
AppendLog "Extract tab:"
AppendLog "  Left list  — drop one or more .minfo files (pl, fp, wp, fn, np)"
AppendLog "  Right list — drop one or more .gts  files"
AppendLog "  Output: output_textures/<charId>/<gts>/"
AppendLog ""
AppendLog "Tip: for a full character, add all five .minfo files:"
AppendLog "  pl1400/pl1400.minfo, fp1400/fp1400.minfo, wp1400/wp1400.minfo, ..."

[void]$form.ShowDialog()
