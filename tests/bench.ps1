# Amber Commander acceptance benchmark
# Run: powershell -ExecutionPolicy Bypass -File tests\bench.ps1

$ErrorActionPreference = 'Stop'
$amber = "$PSScriptRoot\..\amber.exe"
if (-not (Test-Path $amber)) { Write-Host "Build amber.exe first (build.bat)"; exit 1 }

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Psapi {
    [DllImport("psapi.dll", SetLastError=true)]
    public static extern bool GetProcessMemoryInfo(IntPtr h, out PROCESS_MEMORY_COUNTERS_EX c, uint size);
    [StructLayout(LayoutKind.Sequential)]
    public struct PROCESS_MEMORY_COUNTERS_EX {
        public uint cb;
        public uint PageFaultCount;
        public UIntPtr PeakWorkingSetSize;
        public UIntPtr WorkingSetSize;
        public UIntPtr QuotaPeakPagedPoolUsage;
        public UIntPtr QuotaPagedPoolUsage;
        public UIntPtr QuotaPeakNonPagedPoolUsage;
        public UIntPtr QuotaNonPagedPoolUsage;
        public UIntPtr PagefileUsage;
        public UIntPtr PeakPagefileUsage;
        public UIntPtr PrivateUsage;
    }
}
"@

function Get-MemMB($proc) {
    $c = New-Object Psapi+PROCESS_MEMORY_COUNTERS_EX
    [Psapi]::GetProcessMemoryInfo($proc.Handle, [ref]$c, [Runtime.InteropServices.Marshal]::SizeOf($c)) | Out-Null
    [math]::Round($c.WorkingSetSize.ToUInt64() / 1MB, 1)
}

# 1) exe size
$size = (Get-Item $amber).Length
"1) exe size: {0:N0} bytes ({1:N2} MB)" -f $size, ($size / 1MB)

# 2) startup time (until main window appears)
$sw = [Diagnostics.Stopwatch]::StartNew()
$p = Start-Process -FilePath $amber -WorkingDirectory (Split-Path $amber) -PassThru
$deadline = [DateTime]::Now.AddSeconds(10)
while ($p.MainWindowHandle -eq 0 -and [DateTime]::Now -lt $deadline) {
    Start-Sleep -Milliseconds 10
    $p.Refresh()
}
$sw.Stop()
Start-Sleep -Milliseconds 500
"2) Startup to ready: $($sw.ElapsedMilliseconds) ms"
"   Working set: $(Get-MemMB $p) MB"

# 3) idle CPU
$c0 = $p.TotalProcessorTime.TotalMilliseconds
Start-Sleep -Seconds 3
$c1 = $p.TotalProcessorTime.TotalMilliseconds
$cpu = [math]::Round(($c1 - $c0) / 30.0, 2)
"3) Idle CPU (3 s): $cpu %"
"   Working set after 3 s: $(Get-MemMB $p) MB"

Stop-Process -Id $p.Id -Force
"Done."
