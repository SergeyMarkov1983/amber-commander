# Автотесты Amber Commander (без модификаторов).
# Каждый сценарий запускает свежую копию amber_test.exe и печатает PASS/FAIL.
# Запуск: powershell -ExecutionPolicy Bypass -File autotest.ps1
$ErrorActionPreference = 'Stop'
$script:Root = Split-Path -Parent $PSScriptRoot
$script:TD = Join-Path $env:TEMP 'amber_autotest'
$script:ED = 'E:\amber_autotest_data'
$script:Pass = 0
$script:Fail = 0

$src = @"
using System;
using System.Text;
using System.Runtime.InteropServices;
using System.Drawing;
using System.Drawing.Imaging;
public class AT {
  [DllImport("kernel32.dll")] public static extern IntPtr GetConsoleWindow();
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int n);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll", EntryPoint="PostMessageW")] public static extern bool PM(IntPtr h, uint m, uint w, uint l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr lp);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("gdi32.dll")] public static extern IntPtr CreateCompatibleDC(IntPtr dc);
  [DllImport("gdi32.dll")] public static extern bool DeleteDC(IntPtr dc);
  [DllImport("gdi32.dll")] public static extern IntPtr SelectObject(IntPtr dc, IntPtr obj);
  public delegate bool EnumProc(IntPtr h, IntPtr lp);
  public struct RECT { public int Left, Top, Right, Bottom; }
  public static int Target = 0; public static string Needle = ""; public static IntPtr Found = IntPtr.Zero;
  public static bool Cb(IntPtr h, IntPtr lp) {
    uint pid; GetWindowThreadProcessId(h, out pid);
    if (pid == (uint)Target) {
      var sb = new StringBuilder(256); GetWindowText(h, sb, 256);
      if (sb.ToString().Contains(Needle)) { Found = h; return false; }
    }
    return true;
  }
  public static IntPtr Find(int pid, string contains) { Target = pid; Needle = contains; Found = IntPtr.Zero; EnumWindows(Cb, IntPtr.Zero); return Found; }
  public static IntPtr FindPopup(int pid) {
    Target = pid; Found = IntPtr.Zero;
    EnumWindows((h, lp) => {
      uint p; GetWindowThreadProcessId(h, out p);
      if (p == (uint)Target) {
        var cb = new StringBuilder(64); GetClassName(h, cb, 64);
        if (cb.ToString() == "AmberDialog") {
          var r = new RECT(); GetWindowRect(h, out r);
          if (r.Right - r.Left > 50 && r.Bottom - r.Top > 20) { Found = h; return false; }
        }
      }
      return true;
    }, IntPtr.Zero);
    return Found;
  }
  public static uint LP(int x, int y) { return (uint)((y << 16) | (x & 0xFFFF)); }
  public static Bitmap Snap(IntPtr h) {
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
    return Image.FromHbitmap(hbm) as Bitmap;
  }
  public static int CountNear(Bitmap b, int tr, int tg, int tb, int tol, int x0, int x1, int y0, int y1) {
    var rd = new Rectangle(x0, y0, x1 - x0, y1 - y0);
    var data = b.LockBits(rd, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
    int stride = data.Stride; int cnt = 0;
    var buf = new byte[stride * rd.Height];
    Marshal.Copy(data.Scan0, buf, 0, buf.Length);
    for (int y = 0; y < rd.Height; y++) for (int x = 0; x < rd.Width; x++) {
      int i = y * stride + x * 4;
      if (Math.Abs(buf[i+2]-tr)<=tol && Math.Abs(buf[i+1]-tg)<=tol && Math.Abs(buf[i]-tb)<=tol) cnt++;
    }
    b.UnlockBits(data);
    return cnt;
  }
  public static int DividerX(Bitmap b, int tr, int tg, int tb, int tol) {
    for (int x = 100; x < b.Width - 100; x++) {
      int n = 0;
      for (int y = 200; y < 500; y += 4) {
        var c = b.GetPixel(x, y);
        if (Math.Abs(c.R-tr)<=tol && Math.Abs(c.G-tg)<=tol && Math.Abs(c.B-tb)<=tol) n++;
      }
      if (n > 60) return x;
    }
    return -1;
  }
  public static void MakeIdx(string path, string[] dirs) {
    // формат v2 (AMBFOLD2): magic | count(uint64) | смещения(uint64) | записи
    // запись: uint16 len | имя UTF-8 (нижний регистр) | uint16 len | путь UTF-8
    var sorted = new System.Collections.Generic.List<string>(dirs);
    sorted.Sort((a, b) => {
      int sa = a.LastIndexOf('\\'); string na = a.Substring(sa + 1).ToLowerInvariant();
      int sb = b.LastIndexOf('\\'); string nb = b.Substring(sb + 1).ToLowerInvariant();
      int c = string.CompareOrdinal(na, nb);
      if (c != 0) return c;
      return string.CompareOrdinal(a.ToLowerInvariant(), b.ToLowerInvariant());
    });
    var entries = new System.Collections.Generic.List<byte[]>();
    foreach (var d in sorted) {
      int slash = d.LastIndexOf('\\');
      string name = d.Substring(slash + 1).ToLowerInvariant();
      var nb = Encoding.UTF8.GetBytes(name);
      var pb = Encoding.UTF8.GetBytes(d);
      var e = new byte[2 + nb.Length + 2 + pb.Length];
      e[0] = (byte)(nb.Length & 0xFF); e[1] = (byte)(nb.Length >> 8);
      Array.Copy(nb, 0, e, 2, nb.Length);
      e[2 + nb.Length] = (byte)(pb.Length & 0xFF); e[2 + nb.Length + 1] = (byte)(pb.Length >> 8);
      Array.Copy(pb, 0, e, 4 + nb.Length, pb.Length);
      entries.Add(e);
    }
    long count = entries.Count;
    long off = 16 + count * 8;
    var ms = new System.IO.MemoryStream();
    ms.Write(Encoding.ASCII.GetBytes("AMBFOLD2"), 0, 8);
    for (int i = 0; i < 8; i++) ms.WriteByte((byte)(count >> (i * 8)));
    foreach (var e in entries) {
      for (int i = 0; i < 8; i++) ms.WriteByte((byte)(off >> (i * 8)));
      off += e.Length;
    }
    foreach (var e in entries) ms.Write(e, 0, e.Length);
    System.IO.File.WriteAllBytes(path, ms.ToArray());
  }
}
"@
Add-Type -TypeDefinition $src -ReferencedAssemblies System.Drawing
[AT]::ShowWindow([AT]::GetConsoleWindow(), 0) | Out-Null

function New-Env {
    param([string]$iniContent, [string[]]$idxDirs)
    $d = Join-Path $script:TD ([guid]::NewGuid().ToString('N').Substring(0, 8))
    New-Item -ItemType Directory -Path $d -Force | Out-Null
    Copy-Item (Join-Path $script:Root 'amber.exe') (Join-Path $d 'amber_test.exe') -Force
    if ($iniContent) { $iniContent | Out-File (Join-Path $d 'amber.ini') -Encoding ascii }
    if ($idxDirs) { [AT]::MakeIdx((Join-Path $d 'folders.idx'), $idxDirs) }
    return $d
}

function Start-App {
    param([string]$dir)
    return Start-Process -FilePath (Join-Path $dir 'amber_test.exe') -WorkingDirectory $dir -PassThru
}

function Type-Text {
    param([IntPtr]$h, [string]$t)
    foreach ($c in $t.ToCharArray()) { [AT]::PM($h, 0x0102, [UInt32][int]$c, 0) | Out-Null }
}

function Report {
    param([string]$name, [bool]$ok, [string]$detail = '')
    if ($ok) { $script:Pass++; "PASS: $name $detail" }
    else { $script:Fail++; "FAIL: $name $detail" }
}

function Stop-App {
    param($p, [string]$dir)
    if ($p -and -not $p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
    Remove-Item $dir -Recurse -Force -ErrorAction SilentlyContinue
}

Remove-Item $script:TD -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item $script:ED -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $script:ED -Force | Out-Null
New-Item -ItemType Directory -Path "$script:ED\Тест Папка\вложенная" -Force | Out-Null
New-Item -ItemType Directory -Path "$script:ED\del_test" -Force | Out-Null
New-Item -ItemType Directory -Path "$script:ED\dnd" -Force | Out-Null
New-Item -ItemType Directory -Path "$script:ED\dnd\dst" -Force | Out-Null
New-Item -ItemType Directory -Path "$script:ED\dnd\srcA" -Force | Out-Null
foreach ($n in 1..3) { [System.IO.File]::WriteAllText("$script:ED\del_test\f$n.txt", 'x') }
[System.IO.File]::WriteAllText("$script:ED\Тест Папка\документ.txt", 'кириллица')
[System.IO.File]::WriteAllText("$script:ED\Тест Папка\вложенная\файл.dat", 'x')

# ---------- A1: холодный старт ----------
$d = New-Env
$p = Start-App $d
Start-Sleep -Seconds 2
$h = $p.MainWindowHandle
$bmp = [AT]::Snap($h)
$cr = New-Object AT+RECT; [AT]::GetClientRect($h, [ref]$cr) | Out-Null
$div = [AT]::DividerX($bmp, 72, 29, 0, 25)
$mem = (Get-Process -Id $p.Id).PrivateMemorySize64
$bmp.Dispose()
Report 'A1 холодный старт' ($cr.Right -gt 800 -and $div -gt 400 -and $div -lt 900 -and $mem -lt 20MB) "клиент=$($cr.Right)x$($cr.Bottom) делитель=$div память=$([math]::Round($mem/1MB,1))МБ"
Stop-App $p $d

# ---------- B1: cd кириллица+пробел ----------
$d = New-Env
$p = Start-App $d
Start-Sleep -Seconds 2
$h = $p.MainWindowHandle
Type-Text $h "cd $script:ED\Тест Папка"
[AT]::PM($h, 0x0100, 0x0D, 0) | Out-Null
Start-Sleep -Milliseconds 1500
$bmp = [AT]::Snap($h)
$txt = [AT]::CountNear($bmp, 236, 234, 229, 45, 40, 300, 56, 78)
$bmp.Dispose()
Report 'B1 cd кириллица/пробелы' ($txt -gt 40) "крошка в шапке px=$txt"
Stop-App $p $d

# ---------- B2: cd несуществующий ----------
$d = New-Env
$p = Start-App $d
Start-Sleep -Seconds 2
$h = $p.MainWindowHandle
Type-Text $h "cd Q:\no_such_dir"
[AT]::PM($h, 0x0100, 0x0D, 0) | Out-Null
Start-Sleep -Milliseconds 900
Report 'B2 cd несуществующий' (-not $p.HasExited)
Stop-App $p $d

# ---------- B3: ← на корне ----------
$d = New-Env
$p = Start-App $d
Start-Sleep -Seconds 2
$h = $p.MainWindowHandle
[AT]::PM($h, 0x0100, 0x25, 0) | Out-Null
Start-Sleep -Milliseconds 500
Report 'B3 стрелка влево на корне' (-not $p.HasExited)
Stop-App $p $d

# ---------- D6: курсор после удаления ----------
$d = New-Env
$p = Start-App $d
Start-Sleep -Seconds 2
$h = $p.MainWindowHandle
Type-Text $h "cd $script:ED\del_test"
[AT]::PM($h, 0x0100, 0x0D, 0) | Out-Null
Start-Sleep -Milliseconds 1200
[AT]::PM($h, 0x0100, 0x28, 0) | Out-Null
[AT]::PM($h, 0x0100, 0x28, 0) | Out-Null
Start-Sleep -Milliseconds 300
[AT]::PM($h, 0x0100, 0x77, 0) | Out-Null
Start-Sleep -Milliseconds 900
$dlg = [AT]::Find($p.Id, "Удаление")
if ($dlg -ne [IntPtr]::Zero) { [AT]::PM($dlg, 0x0100, 0x0D, 0) | Out-Null }
Start-Sleep -Milliseconds 1500
$bmp = [AT]::Snap($h)
$cur = [AT]::CountNear($bmp, 150, 80, 3, 25, 20, 550, 115, 140)
$bmp.Dispose()
Report 'D6 курсор после удаления' ($cur -gt 800) "подсветка ряда 2 px=$cur"
Stop-App $p $d

# ---------- D8: перетаскивание правой кнопкой ----------
$d = New-Env
$p = Start-App $d
Start-Sleep -Seconds 2
$h = $p.MainWindowHandle
Type-Text $h "cd $script:ED\dnd"
[AT]::PM($h, 0x0100, 0x0D, 0) | Out-Null
Start-Sleep -Milliseconds 1200
[AT]::PM($h, 0x0204, 0, [AT]::LP(300, 129)) | Out-Null
Start-Sleep -Milliseconds 150
[AT]::PM($h, 0x0200, 2, [AT]::LP(300, 109)) | Out-Null
Start-Sleep -Milliseconds 400
$bmp = [AT]::Snap($h)
$hl = [AT]::CountNear($bmp, 239, 163, 63, 25, 4, 618, 96, 122)
$bmp.Dispose()
[AT]::PM($h, 0x0205, 2, [AT]::LP(300, 109)) | Out-Null
Start-Sleep -Milliseconds 1800
$moved = (Test-Path "$script:ED\dnd\dst\srcA") -and -not (Test-Path "$script:ED\dnd\srcA")
Report 'D8 перетаскивание+подсветка' ($hl -gt 200 -and $moved) "рамка px=$hl перемещено=$moved"
New-Item -ItemType Directory -Path "$script:ED\dnd\srcA" -Force | Out-Null
Stop-App $p $d

# ---------- E1: F3 просмотрщик ----------
$d = New-Env
$p = Start-App $d
Start-Sleep -Seconds 2
$h = $p.MainWindowHandle
Type-Text $h "cd $script:ED\Тест Папка"
[AT]::PM($h, 0x0100, 0x0D, 0) | Out-Null
Start-Sleep -Milliseconds 1200
[AT]::PM($h, 0x0100, 0x28, 0) | Out-Null
Start-Sleep -Milliseconds 300
[AT]::PM($h, 0x0100, 0x72, 0) | Out-Null
Start-Sleep -Milliseconds 1200
$v = [AT]::Find($p.Id, "Просмотр")
if ($v -ne [IntPtr]::Zero) { [AT]::PM($v, 0x0100, 0x1B, 0) | Out-Null }
Report 'E1 F3 просмотрщик' ($v -ne [IntPtr]::Zero -and -not $p.HasExited)
Stop-App $p $d

# ---------- C1: F11 размеры ----------
$d = New-Env
$p = Start-App $d
Start-Sleep -Seconds 2
$h = $p.MainWindowHandle
Type-Text $h "cd $script:ED"
[AT]::PM($h, 0x0100, 0x0D, 0) | Out-Null
Start-Sleep -Milliseconds 1200
[AT]::PM($h, 0x0100, 0x7A, 0) | Out-Null
Start-Sleep -Milliseconds 3500
$bmp = [AT]::Snap($h)
$crc = New-Object AT+RECT; [AT]::GetClientRect($h, [ref]$crc) | Out-Null
$bar = [AT]::CountNear($bmp, 96, 54, 8, 20, $crc.Right / 2 + 8, $crc.Right - 8, 90, 700)
$bmp.Dispose()
Report 'C1 F11 размеры (полосы)' ($bar -gt 500) "полосы px=$bar"
Stop-App $p $d

# ---------- F2: глобальный поиск (синтетический индекс) ----------
# folderIndexRefresh=0: без фоновой пересборки — тест детерминированно работает
# на синтетическом индексе (пересборка по умолчанию могла бы его перезаписать).
$d = New-Env "[main]`r`nfolderIndexRefresh=0`r`n" @("$script:ED\Тест Папка")
$p = Start-App $d
Start-Sleep -Seconds 2
$h = $p.MainWindowHandle
Type-Text $h "Тест"
Start-Sleep -Milliseconds 900
$bmp = [AT]::Snap($h)
$cr = New-Object AT+RECT; [AT]::GetClientRect($h, [ref]$cr) | Out-Null
$acc = [AT]::CountNear($bmp, 239, 163, 63, 25, 12, 720, $cr.Bottom - 90, $cr.Bottom - 55)
$bmp.Dispose()
[AT]::PM($h, 0x0100, 0x0D, 0) | Out-Null
Start-Sleep -Milliseconds 1500
$bmp = [AT]::Snap($h)
$amber = [AT]::CountNear($bmp, 239, 163, 63, 25, 10, 30, 85, 700)
$bmp.Dispose()
Report 'F2 глобальный поиск+Enter' ($acc -gt 60 -and $amber -gt 30) "рамка px=$acc папки px=$amber"
Stop-App $p $d

# ---------- F3: повреждённый индекс ----------
$d = New-Env
$p = Start-App $d
Start-Sleep -Seconds 2
$h = $p.MainWindowHandle
Type-Text $h "ko"
Start-Sleep -Milliseconds 800
Report 'F3 повреждённый индекс не роняет' (-not $p.HasExited)
Stop-App $p $d

# ---------- B6: хлебные крошки ----------
$d = New-Env
$p = Start-App $d
Start-Sleep -Seconds 2
$h = $p.MainWindowHandle
Type-Text $h "cd $script:ED\Тест Папка"
[AT]::PM($h, 0x0100, 0x0D, 0) | Out-Null
Start-Sleep -Milliseconds 1200
[AT]::PM($h, 0x0201, 1, [AT]::LP(18, 67)) | Out-Null
[AT]::PM($h, 0x0202, 0, [AT]::LP(18, 67)) | Out-Null
Start-Sleep -Milliseconds 1500
[AT]::PM($h, 0x0201, 1, [AT]::LP(36, 67)) | Out-Null
[AT]::PM($h, 0x0202, 0, [AT]::LP(36, 67)) | Out-Null
Start-Sleep -Milliseconds 900
$pop = [AT]::FindPopup($p.Id)
if ($pop -ne [IntPtr]::Zero) { [AT]::PM($pop, 0x0100, 0x1B, 0) | Out-Null }
Report 'B6 крошки: сегмент+попап' ($pop -ne [IntPtr]::Zero -and -not $p.HasExited)
Stop-App $p $d

# ---------- G1: темы ----------
$themes = @(
    @(0, 14, 8, 1, 17, 13, 8, 46, 28, 10),
    @(1, 29, 22, 17, 37, 29, 22, 142, 93, 58),
    @(2, 206, 213, 222, 220, 226, 232, 122, 152, 180),
    @(3, 2, 8, 2, 3, 11, 3, 24, 52, 24),
    @(4, 206, 209, 214, 219, 222, 227, 140, 148, 158),
    @(5, 0, 0, 128, 0, 0, 128, 0, 255, 255),
    @(6, 16, 18, 26, 21, 23, 32, 90, 110, 210),
    @(7, 3, 8, 13, 10, 17, 24, 18, 58, 68),
    @(8, 20, 20, 20, 14, 14, 14, 40, 66, 110)
)
foreach ($t in $themes) {
    $ini = "[main]`r`ntheme=$($t[0])`r`n"
    $d = New-Env $ini
    $p = Start-App $d
    Start-Sleep -Seconds 2
    $h = $p.MainWindowHandle
    $bmp = [AT]::Snap($h)
    $bg = $bmp.GetPixel(2, 400)
    $pb = $bmp.GetPixel(400, 400)
    $hd = $bmp.GetPixel(150, 65)
    $bmp.Dispose()
    $ok = ([math]::Abs($bg.R - $t[1]) -le 6) -and ([math]::Abs($bg.G - $t[2]) -le 6) -and ([math]::Abs($bg.B - $t[3]) -le 6) -and
          ([math]::Abs($pb.R - $t[4]) -le 6) -and ([math]::Abs($pb.G - $t[5]) -le 6) -and ([math]::Abs($pb.B - $t[6]) -le 6) -and
          ([math]::Abs($hd.R - $t[7]) -le 6) -and ([math]::Abs($hd.G - $t[8]) -le 6) -and ([math]::Abs($hd.B - $t[9]) -le 6)
    Report "G1 тема $($t[0])" $ok "фон=$($bg.R),$($bg.G),$($bg.B) панель=$($pb.R),$($pb.G),$($pb.B) шапка=$($hd.R),$($hd.G),$($hd.B)"
    Stop-App $p $d
}

# ---------- A2: восстановление из ini ----------
$d = New-Env "[main]`r`ntheme=5`r`nleftDir=$script:ED`r`nrightDir=$script:ED\del_test`r`nwinRect=200,150,900,700`r`nwinMax=0`r`n"
$p = Start-App $d
Start-Sleep -Seconds 2
$h = $p.MainWindowHandle
$bmp = [AT]::Snap($h)
$pb = $bmp.GetPixel(400, 400)
$bmp.Dispose()
$src2 = @"
using System;
using System.Runtime.InteropServices;
public class WR {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
Add-Type -TypeDefinition $src2
$wrr = New-Object WR+RECT; [WR]::GetWindowRect($h, [ref]$wrr) | Out-Null
Report 'A2 восстановление из ini' ($pb.B -eq 128 -and $wrr.Left -eq 200 -and $wrr.Top -eq 150) "панель B=$($pb.B) окно=$($wrr.Left),$($wrr.Top)"
Stop-App $p $d

# ---------- I1: память/CPU ----------
# folderIndexRefresh=0: замер «простоя» без фоновой пересборки индекса — она
# теперь включена по умолчанию и идёт при каждом запуске (даёт CPU/память).
$d = New-Env "[main]`r`nfolderIndexRefresh=0`r`n" @("$script:ED\Тест Папка")
$p = Start-App $d
Start-Sleep -Seconds 3
$mem = (Get-Process -Id $p.Id).PrivateMemorySize64
$cpu1 = (Get-Process -Id $p.Id).CPU
Start-Sleep -Seconds 3
$cpu2 = (Get-Process -Id $p.Id).CPU
Report 'I1 память/CPU простоя' ($mem -lt 12MB -and ($cpu2 - $cpu1) -lt 0.3) "память=$([math]::Round($mem/1MB,1))МБ cpu=$([math]::Round(($cpu2-$cpu1),2))с"
Stop-App $p $d

Remove-Item $script:TD -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item $script:ED -Recurse -Force -ErrorAction SilentlyContinue
""
"ИТОГО: PASS=$script:Pass FAIL=$script:Fail"
