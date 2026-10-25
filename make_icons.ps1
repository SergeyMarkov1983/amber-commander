# Amber Commander - icon generator.
# Takes res\icons_source.png (2x2 grid of icon variants),
# makes multi-size ICO files:
#   res\amber.ico     - top-left quarter  (main app icon)
#   res\amber_alt.ico - bottom-left quarter (alternative)
Add-Type -AssemblyName System.Drawing
$src = @"
using System;
using System.IO;
using System.Drawing;
using System.Drawing.Imaging;
using System.Drawing.Drawing2D;
using System.Collections.Generic;
public class IcoGen {
    public static Bitmap Crop(Bitmap src, int qx, int qy) {
        int w = src.Width / 2, h = src.Height / 2;
        var bmp = new Bitmap(w, h);
        using (var g = Graphics.FromImage(bmp)) {
            g.InterpolationMode = InterpolationMode.HighQualityBicubic;
            g.PixelOffsetMode = PixelOffsetMode.HighQuality;
            g.DrawImage(src, new Rectangle(0, 0, w, h), qx * w, qy * h, w, h, GraphicsUnit.Pixel);
        }
        return bmp;
    }
    public static void SaveIco(string path, Bitmap src, int[] sizes) {
        var pngs = new List<byte[]>();
        foreach (int s in sizes) {
            using (var bmp = new Bitmap(src, s, s)) {
                var ms = new MemoryStream();
                bmp.Save(ms, ImageFormat.Png);
                pngs.Add(ms.ToArray());
            }
        }
        var mem = new MemoryStream();
        var bw = new BinaryWriter(mem);
        bw.Write((ushort)0); bw.Write((ushort)1); bw.Write((ushort)sizes.Length);
        int offset = 6 + 16 * sizes.Length;
        for (int i = 0; i < sizes.Length; i++) {
            int s = sizes[i];
            bw.Write((byte)(s >= 256 ? 0 : s));
            bw.Write((byte)(s >= 256 ? 0 : s));
            bw.Write((byte)0);    // palette
            bw.Write((byte)0);    // reserved
            bw.Write((ushort)1);  // planes
            bw.Write((ushort)32); // bpp
            bw.Write((uint)pngs[i].Length);
            bw.Write((uint)offset);
            offset += pngs[i].Length;
        }
        foreach (var p in pngs) bw.Write(p);
        File.WriteAllBytes(path, mem.ToArray());
    }
}
"@
Add-Type -TypeDefinition $src -ReferencedAssemblies System.Drawing
$png = Join-Path $PSScriptRoot 'res\icons_source.png'
if (-not (Test-Path $png)) {
    Write-Host "res\icons_source.png not found"
    exit 1
}
$img = [System.Drawing.Image]::FromFile($png)
$sizes = @(16, 20, 24, 32, 48, 64, 128, 256)
$main = [IcoGen]::Crop($img, 0, 0)   # top-left quarter
$alt  = [IcoGen]::Crop($img, 0, 1)   # bottom-left quarter
[IcoGen]::SaveIco((Join-Path $PSScriptRoot 'res\amber.ico'), $main, $sizes)
[IcoGen]::SaveIco((Join-Path $PSScriptRoot 'res\amber_alt.ico'), $alt, $sizes)
$main.Dispose(); $alt.Dispose(); $img.Dispose()
Write-Host "amber.ico (top-left) and amber_alt.ico (bottom-left) generated"
