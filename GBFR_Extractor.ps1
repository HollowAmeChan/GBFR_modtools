# GBFR_Extractor.ps1 - GUI texture extractor
# Drop .minfo + .gts into the window, click Extract.

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$scriptDir  = Split-Path $PSCommandPath -Parent
$graniteExe = Join-Path $scriptDir "GraniteTextureReader.exe"
$hashesDir  = Join-Path $scriptDir "output_hashes"
$outputDir  = Join-Path $scriptDir "output_textures"

$script:MinfoPath = $null
$script:GtsPath   = $null
$script:Working   = $false
$script:LogQueue  = [System.Collections.Concurrent.ConcurrentQueue[string]]::new()
$script:ProgQueue = [System.Collections.Concurrent.ConcurrentQueue[int]]::new()

# ── colours ──────────────────────────────────────────────────────────────────
$clrBg      = [System.Drawing.Color]::FromArgb(30,  30,  30)
$clrPanel   = [System.Drawing.Color]::FromArgb(45,  45,  48)
$clrPanelOk = [System.Drawing.Color]::FromArgb(30,  70,  40)
$clrAccent  = [System.Drawing.Color]::FromArgb(0,  120, 215)
$clrText    = [System.Drawing.Color]::White
$clrSub     = [System.Drawing.Color]::FromArgb(180, 180, 180)
$font       = New-Object System.Drawing.Font("Segoe UI", 10)
$fontMono   = New-Object System.Drawing.Font("Consolas",  9)
$fontBig    = New-Object System.Drawing.Font("Segoe UI", 12, [System.Drawing.FontStyle]::Bold)

# ── form ─────────────────────────────────────────────────────────────────────
$form = New-Object System.Windows.Forms.Form
$form.Text            = "GBFR Texture Extractor"
$form.ClientSize      = New-Object System.Drawing.Size(720, 560)
$form.MinimumSize     = New-Object System.Drawing.Size(600, 480)
$form.BackColor       = $clrBg
$form.ForeColor       = $clrText
$form.Font            = $font
$form.StartPosition   = "CenterScreen"

# ── helper: make a drop panel ────────────────────────────────────────────────
function MakeDropPanel($x, $y, $w, $h, $ext, $hint) {
    $p = New-Object System.Windows.Forms.Panel
    $p.Location    = New-Object System.Drawing.Point($x, $y)
    $p.Size        = New-Object System.Drawing.Size($w, $h)
    $p.BackColor   = $clrPanel
    $p.AllowDrop   = $true
    $p.Cursor      = [System.Windows.Forms.Cursors]::Hand

    $lbl = New-Object System.Windows.Forms.Label
    $lbl.Text      = $hint
    $lbl.Dock      = "Fill"
    $lbl.TextAlign = "MiddleCenter"
    $lbl.Font      = $fontBig
    $lbl.ForeColor = $clrSub
    $lbl.BackColor = [System.Drawing.Color]::Transparent
    $p.Controls.Add($lbl)

    # hover highlight
    $p.Add_MouseEnter({ $this.BackColor = [System.Drawing.Color]::FromArgb(55,55,60) })
    $p.Add_MouseLeave({
        if (-not $this.Tag) { $this.BackColor = $clrPanel }
    })

    # drag accept
    $p.Add_DragEnter({
        if ($_.Data.GetDataPresent([System.Windows.Forms.DataFormats]::FileDrop)) {
            $_.Effect = "Copy"
        }
    })

    return $p, $lbl
}

# ── drop zone A : .minfo ─────────────────────────────────────────────────────
$pA, $lblA = MakeDropPanel 10 10 345 110 ".minfo" "Drop  .minfo  here`n(click to browse)"

$pA.Add_DragDrop({
    $f = ($_.Data.GetData([System.Windows.Forms.DataFormats]::FileDrop))[0]
    if ([IO.Path]::GetExtension($f) -ieq ".minfo") {
        $script:MinfoPath = $f
        $lblA.Text        = [IO.Path]::GetFileName($f)
        $pA.BackColor     = $clrPanelOk
        $pA.Tag           = $true
        UpdateBtn
    } else {
        [System.Windows.Forms.MessageBox]::Show("Expected a .minfo file.", "Wrong type")
    }
})

$pA.Add_Click({
    $d = New-Object System.Windows.Forms.OpenFileDialog
    $d.Filter = "Model info (*.minfo)|*.minfo"
    if ($d.ShowDialog() -eq "OK") {
        $script:MinfoPath = $d.FileName
        $lblA.Text        = [IO.Path]::GetFileName($d.FileName)
        $pA.BackColor     = $clrPanelOk
        $pA.Tag           = $true
        UpdateBtn
    }
})
$lblA.Add_Click({ $pA.PerformClick() })

# ── drop zone B : .gts ───────────────────────────────────────────────────────
$pB, $lblB = MakeDropPanel 365 10 345 110 ".gts" "Drop  .gts  here`n(click to browse)"

$pB.Add_DragDrop({
    $f = ($_.Data.GetData([System.Windows.Forms.DataFormats]::FileDrop))[0]
    if ([IO.Path]::GetExtension($f) -ieq ".gts") {
        $script:GtsPath = $f
        $lblB.Text      = [IO.Path]::GetFileName($f)
        $pB.BackColor   = $clrPanelOk
        $pB.Tag         = $true
        UpdateBtn
    } else {
        [System.Windows.Forms.MessageBox]::Show("Expected a .gts file.", "Wrong type")
    }
})

$pB.Add_Click({
    $d = New-Object System.Windows.Forms.OpenFileDialog
    $d.Filter = "Granite tile set (*.gts)|*.gts"
    if ($d.ShowDialog() -eq "OK") {
        $script:GtsPath = $d.FileName
        $lblB.Text      = [IO.Path]::GetFileName($d.FileName)
        $pB.BackColor   = $clrPanelOk
        $pB.Tag         = $true
        UpdateBtn
    }
})
$lblB.Add_Click({ $pB.PerformClick() })

# ── buttons ───────────────────────────────────────────────────────────────────
$btnExtract = New-Object System.Windows.Forms.Button
$btnExtract.Text      = "Extract"
$btnExtract.Location  = New-Object System.Drawing.Point(10, 130)
$btnExtract.Size      = New-Object System.Drawing.Size(120, 34)
$btnExtract.BackColor = $clrAccent
$btnExtract.ForeColor = $clrText
$btnExtract.FlatStyle = "Flat"
$btnExtract.Enabled   = $false

$btnClear = New-Object System.Windows.Forms.Button
$btnClear.Text      = "Clear log"
$btnClear.Location  = New-Object System.Drawing.Point(140, 130)
$btnClear.Size      = New-Object System.Drawing.Size(90, 34)
$btnClear.BackColor = $clrPanel
$btnClear.ForeColor = $clrSub
$btnClear.FlatStyle = "Flat"
$btnClear.Add_Click({ $txtLog.Clear() })

$btnOpenOut = New-Object System.Windows.Forms.Button
$btnOpenOut.Text      = "Open output folder"
$btnOpenOut.Location  = New-Object System.Drawing.Point(240, 130)
$btnOpenOut.Size      = New-Object System.Drawing.Size(150, 34)
$btnOpenOut.BackColor = $clrPanel
$btnOpenOut.ForeColor = $clrSub
$btnOpenOut.FlatStyle = "Flat"
$btnOpenOut.Add_Click({
    if (Test-Path $outputDir) { explorer.exe $outputDir }
})

# ── progress bar ─────────────────────────────────────────────────────────────
$prog = New-Object System.Windows.Forms.ProgressBar
$prog.Location = New-Object System.Drawing.Point(10, 172)
$prog.Size     = New-Object System.Drawing.Size(700, 14)
$prog.Minimum  = 0
$prog.Maximum  = 100
$prog.Style    = "Continuous"
$prog.BackColor = $clrPanel

# ── log box ───────────────────────────────────────────────────────────────────
$txtLog = New-Object System.Windows.Forms.TextBox
$txtLog.Multiline   = $true
$txtLog.ScrollBars  = "Vertical"
$txtLog.ReadOnly    = $true
$txtLog.Location    = New-Object System.Drawing.Point(10, 192)
$txtLog.Size        = New-Object System.Drawing.Size(700, 350)
$txtLog.Font        = $fontMono
$txtLog.BackColor   = [System.Drawing.Color]::FromArgb(20, 20, 20)
$txtLog.ForeColor   = $clrText
$txtLog.BorderStyle = "None"

# ── layout on resize ─────────────────────────────────────────────────────────
$form.Add_Resize({
    $w = $form.ClientSize.Width
    $h = $form.ClientSize.Height
    $half = [int](($w - 30) / 2)
    $pA.Width         = $half
    $pB.Width         = $half
    $pB.Left          = $pA.Right + 10
    $prog.Width       = $w - 20
    $txtLog.Width     = $w - 20
    $txtLog.Height    = $h - 202
})

# ── add all controls ─────────────────────────────────────────────────────────
$form.Controls.AddRange(@($pA, $pB, $btnExtract, $btnClear, $btnOpenOut, $prog, $txtLog))

function UpdateBtn {
    $btnExtract.Enabled = ($null -ne $script:MinfoPath) -and
                          ($null -ne $script:GtsPath)   -and
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

# ── extraction (background runspace) ─────────────────────────────────────────
$btnExtract.Add_Click({
    if ($script:Working) { return }
    $script:Working = $true
    UpdateBtn
    $txtLog.Clear()
    $prog.Value = 0

    $rs = [runspacefactory]::CreateRunspace()
    $rs.ApartmentState = "STA"
    $rs.ThreadOptions  = "ReuseThread"
    $rs.Open()
    $rs.SessionStateProxy.SetVariable("MinfoPath",   $script:MinfoPath)
    $rs.SessionStateProxy.SetVariable("GtsPath",     $script:GtsPath)
    $rs.SessionStateProxy.SetVariable("GraniteExe",  $graniteExe)
    $rs.SessionStateProxy.SetVariable("HashesDir",   $hashesDir)
    $rs.SessionStateProxy.SetVariable("OutputDir",   $outputDir)
    $rs.SessionStateProxy.SetVariable("LogQ",        $script:LogQueue)
    $rs.SessionStateProxy.SetVariable("ProgQ",       $script:ProgQueue)

    $ps = [powershell]::Create()
    $ps.Runspace = $rs
    $ps.AddScript({
        function L($m) { $LogQ.Enqueue($m) }

        # ── step 1 ───────────────────────────────────────────────────────────
        L "=== Step 1: parsing mmat files ==="
        $minfoDir = Split-Path $MinfoPath -Parent
        $charName = (Split-Path $MinfoPath -Leaf) -replace '\.minfo$',''
        $varsDir  = Join-Path $minfoDir "vars"

        if (-not (Test-Path $varsDir)) {
            L "[ERROR] vars directory not found: $varsDir"; return
        }

        $mmatFiles = Get-ChildItem "$varsDir\*.mmat" -EA SilentlyContinue |
                     Sort-Object { [int]($_.BaseName) }
        L "Found $($mmatFiles.Count) mmat file(s)"

        $entries  = [System.Collections.Generic.List[PSObject]]::new()
        $seenHash = [System.Collections.Generic.HashSet[string]]::new()
        $nameRx   = [regex]'([a-z][a-z0-9_]{3,60})'
        $hashRx   = [regex]'[0-9a-f]{64}'

        foreach ($mmat in $mmatFiles) {
            $text = [System.Text.Encoding]::ASCII.GetString(
                        [System.IO.File]::ReadAllBytes($mmat.FullName))
            foreach ($hm in $hashRx.Matches($text)) {
                $h = $hm.Value
                if ($seenHash.Contains($h)) { continue }
                $seenHash.Add($h) | Out-Null
                $after = $text.Substring($hm.Index+64, [Math]::Min(120,$text.Length-$hm.Index-64))
                $nm    = $nameRx.Match($after)
                $name  = if ($nm.Success) { $nm.Value } else { "slot_$($h.Substring(0,8))" }
                $entries.Add([PSCustomObject]@{Hash=$h;Name=$name;Source=$mmat.Name})
            }
        }

        L "Unique hashes: $($entries.Count)"

        New-Item -ItemType Directory -Force -Path $HashesDir | Out-Null
        $hashFile = Join-Path $HashesDir "${charName}_hashes.txt"
        $lines = @("# GBFR Hash Table  $charName  $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')")
        foreach ($e in $entries) {
            $lines += "$($e.Hash)  $($e.Name)  [$($e.Source)]"
        }
        $lines | Set-Content $hashFile -Encoding ASCII
        L "Hash table -> $hashFile"

        # ── step 2 ───────────────────────────────────────────────────────────
        L ""
        L "=== Step 2: extracting from $([IO.Path]::GetFileName($GtsPath)) ==="

        if (-not (Test-Path $GraniteExe)) {
            L "[ERROR] GraniteTextureReader.exe not found: $GraniteExe"; return
        }

        $outDir  = Join-Path $OutputDir $charName
        New-Item -ItemType Directory -Force -Path $outDir | Out-Null

        $ok = 0; $fail = 0; $total = $entries.Count
        $tmp = Join-Path $env:TEMP "gtr_err.txt"

        foreach ($entry in $entries) {
            $n    = $ok + $fail + 1
            $proc = Start-Process -FilePath $GraniteExe `
                        -ArgumentList @("extract",
                                        "-t", "`"$GtsPath`"",
                                        "-f", $entry.Hash,
                                        "-l", "-1",
                                        "-o", "`"$outDir`"") `
                        -Wait -PassThru -NoNewWindow `
                        -RedirectStandardError $tmp

            if ($proc.ExitCode -eq 0) {
                $ok++
                L "  OK  [$n/$total]  $($entry.Name)"
            } else {
                $fail++
                $err = (Get-Content $tmp -Raw -EA SilentlyContinue) -replace"`n"," "
                L "  !!  [$n/$total]  $($entry.Name)  =>  $err"
            }
            $ProgQ.Enqueue([int](($n / $total) * 100))
        }

        L ""
        L "Done.  OK: $ok   Failed: $fail"
        L "Output: $outDir"
        L "##DONE##"
    }) | Out-Null

    $handle = $ps.BeginInvoke()

    $timer = New-Object System.Windows.Forms.Timer
    $timer.Interval = 80
    $timer.Add_Tick({
        $msg = ""; $pct = 0
        while ($script:LogQueue.TryDequeue([ref]$msg)) {
            AppendLog $msg
        }
        while ($script:ProgQueue.TryDequeue([ref]$pct)) {
            $prog.Value = [Math]::Min($pct, 100)
        }
        if ($handle.IsCompleted) {
            $timer.Stop()
            try { $ps.EndInvoke($handle) } catch {}
            $ps.Dispose(); $rs.Dispose()
            $script:Working = $false
            $prog.Value = 100
            UpdateBtn
        }
    })
    $timer.Start()
})

# ── show ──────────────────────────────────────────────────────────────────────
AppendLog "GBFR Texture Extractor ready."
if (-not (Test-Path $graniteExe)) {
    AppendLog "[WARN] GraniteTextureReader.exe not found in this folder."
    AppendLog "       Download: https://github.com/Nenkai/GraniteTextureReader/releases"
}

[void]$form.ShowDialog()
