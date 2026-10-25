Add-Type -AssemblyName System.Drawing
$size = 256
$bmp = New-Object System.Drawing.Bitmap($size, $size)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
$g.Clear([System.Drawing.Color]::FromArgb(0,0,0,0))

function RoundedRect($x,$y,$w,$h,$r,$color) {
    $p = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = $r * 2
    $p.AddArc($x, $y, $d, $d, 180, 90)
    $p.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
    $p.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90)
    $p.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
    $p.CloseFigure()
    $br = New-Object System.Drawing.SolidBrush($color)
    $g.FillPath($br, $p)
    $br.Dispose(); $p.Dispose()
}

# Плитка в цвете тёмной темы «Кофе/Какао»
RoundedRect 8 8 240 240 52 ([System.Drawing.Color]::FromArgb(255, 62, 46, 38))
# светлая кромка сверху
$edge = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(90, 234, 216, 188), 6)
$g.DrawArc($edge, 18, 18, 220, 220, 20, 60)
$edge.Dispose()

# Буква «А» — янтарная, крупная, читается на любом размере
$font = New-Object System.Drawing.Font("Georgia", 158, [System.Drawing.FontStyle]::Bold, [System.Drawing.GraphicsUnit]::Pixel)
$amber = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 255, 180, 84))
$sf = New-Object System.Drawing.StringFormat
$sf.Alignment = [System.Drawing.StringAlignment]::Center
$sf.LineAlignment = [System.Drawing.StringAlignment]::Center
$g.DrawString("А", $font, $amber, (New-Object System.Drawing.RectangleF(0, 6, 256, 238)), $sf)
$amber.Dispose(); $font.Dispose(); $sf.Dispose()

# тёплый блик-капля на букве (карамель)
$hl = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(90, 255, 236, 206))
$g.FillEllipse($hl, 52, 40, 40, 26)
$hl.Dispose()

$g.Dispose()
$icon = [System.Drawing.Icon]::FromHandle($bmp.GetHicon())
$fs = [System.IO.File]::Create("$PSScriptRoot\res\amber.ico")
$icon.Save($fs)
$fs.Close()
$icon.Dispose()
$bmp.Dispose()
Write-Host "amber.ico generated (letter A)"
