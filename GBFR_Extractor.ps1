# GBFR_Extractor.ps1  —  multi-minfo x multi-gts GUI extractor + pack placeholder
# Drop multiple .minfo and .gts files into their lists, then Extract.
# Output is organised as: output_textures/<charId>/<gts_stem>/

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$scriptDir  = Split-Path $PSCommandPath -Parent
$graniteExe = Join-Path $scriptDir "GraniteTextureReader.exe"
$nierDir    = Get-ChildItem $scriptDir -Directory -Filter "nier_cli_mgrr_*" |
              Select-Object -First 1 -ExpandProperty FullName
$nierExe    = if ($nierDir) { Join-Path $nierDir "nier_cli_mgrr.exe" } else { $null }
$hashesDir  = Join-Path $scriptDir "output_hashes"
$outputDir  = Join-Path $scriptDir "output_textures"

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
#  PACK TAB  (nier_cli placeholder)
# --------------------------------------------------------------------------
$lblPackTitle = New-Object Windows.Forms.Label
$lblPackTitle.Text     = "Pack textures  (nier_cli_mgrr)"
$lblPackTitle.Font     = $fBig
$lblPackTitle.Location = New-Object Drawing.Point(20, 20)
$lblPackTitle.AutoSize = $true
$tabPack.Controls.Add($lblPackTitle)

$lblNierStatus = New-Object Windows.Forms.Label
$lblNierStatus.Location = New-Object Drawing.Point(20, 55)
$lblNierStatus.AutoSize = $true

if ($nierExe -and (Test-Path $nierExe)) {
    $lblNierStatus.Text      = "nier_cli_mgrr found:  $nierExe"
    $lblNierStatus.ForeColor = [Drawing.Color]::FromArgb(80,200,80)
} else {
    $lblNierStatus.Text      = "nier_cli_mgrr not found.  Place the nier_cli_mgrr_<ver>/ folder next to this script."
    $lblNierStatus.ForeColor = $C.Warn
}
$tabPack.Controls.Add($lblNierStatus)

$lblPackInfo = New-Object Windows.Forms.Label
$lblPackInfo.Text = @"
Workflow (coming in next version):

  1. Edit textures extracted to output_textures/<charId>/<gts>/
  2. Drop the edited folder here → nier_cli repacks to .wtb
  3. Place .wtb in your Reloaded-II mod folder

Manual command:
  nier_cli_mgrr.exe wtbPack -i <folder_with_dds> -o <output.wtb>
"@
$lblPackInfo.Location  = New-Object Drawing.Point(20, 90)
$lblPackInfo.Size      = New-Object Drawing.Size(700, 200)
$lblPackInfo.ForeColor = $C.Sub
$lblPackInfo.Font      = $fMono
$tabPack.Controls.Add($lblPackInfo)

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
