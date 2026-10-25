# Захват скриншотов тем Amber Commander (PrintWindow) для README.
$ErrorActionPreference = 'Stop'
$src = @"
using System;
using System.Runtime.InteropServices;
using System.Drawing;
using System.Drawing.Imaging;
public class SHOT {
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("gdi32.dll")] public static extern IntPtr CreateCompatibleDC(IntPtr dc);
  [DllImport("gdi32.dll")] public static extern bool DeleteDC(IntPtr dc);
  [DllImport("gdi32.dll")] public static extern IntPtr SelectObject(IntPtr dc, IntPtr obj);
  public struct RECT { public int Left, Top, Right, Bottom; }
  public static void Snap(IntPtr h, string path) {
    var rc = new RECT(); GetClientRect(h, out rc);
    var bmp = new Bitmap(Math.Max(1, rc.Right), Math.Max(1, rc.Bottom), PixelFormat.Format32bppArgb);
    var g = Graphics.FromImage(bmp);
    IntPtr hdc = g.GetHdc();
    IntPtr hbm = bmp.GetHbitmap();
    IntPtr sdc = CreateCompatibleDC(hdc);
    IntPtr old = SelectObject(sdc, hbm);
    PrintWindow(h, sdc, 0);
    SelectObject(sdc, old); DeleteDC(sdc);
    g.ReleaseHdc(hdc); g.Dispose();
    // важно: PrintWindow рисует в неуправляемый hbm, а не в bmp
    using (var shot = Image.FromHbitmap(hbm) as Bitmap) shot.Save(path, ImageFormat.Png);
    bmp.Dispose();
  }
}
"@
Add-Type -TypeDefinition $src -ReferencedAssemblies System.Drawing

$root = $PSScriptRoot
$outDir = Join-Path $root 'docs\screenshots'
$td = Join-Path $env:TEMP 'amber_shot'
Remove-Item $td -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $td -Force | Out-Null
Copy-Item (Join-Path $root 'amber.exe') (Join-Path $td 'amber_shot.exe') -Force

$left = 'C:\Windows'
$right = 'C:\Program Files'
$themes = @{ 'amber' = 0; 'phosphor' = 3; 'far' = 5; 'coal' = 8 }

foreach ($k in $themes.Keys) {
    $ini = @"
[main]
theme=$($themes[$k])
leftDir=$left
rightDir=$right
winRect=80,60,1240,800
"@
    $ini | Out-File (Join-Path $td 'amber.ini') -Encoding ascii
    $p = Start-Process -FilePath (Join-Path $td 'amber_shot.exe') -WorkingDirectory $td -PassThru
    Start-Sleep -Seconds 3
    [SHOT]::Snap($p.MainWindowHandle, (Join-Path $outDir "theme_$k.png"))
    Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 500
}
Remove-Item $td -Recurse -Force -ErrorAction SilentlyContinue
Get-ChildItem $outDir -Filter *.png | ForEach-Object { "$($_.Name): $([math]::Round($_.Length/1KB)) КБ" }
