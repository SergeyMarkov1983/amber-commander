# Amber Commander - system integration: Start menu search + command line.
# Run:  powershell -ExecutionPolicy Bypass -File install.ps1
# After that:
#   * Start search finds "Amber Commander" instantly
#   * `amber` works in cmd, PowerShell and Win+R (open a NEW console window)
# If you move the folder later - run this script again.
$ErrorActionPreference = 'Stop'

$dir = $PSScriptRoot
$exe = Join-Path $dir 'amber.exe'
if (-not (Test-Path $exe)) {
    Write-Host "amber.exe not found in $dir - run this script from the Amber folder."
    exit 1
}

# 1) Start menu shortcut (indexed by Windows 11 search by default)
$sm = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs'
$lnkPath = Join-Path $sm 'Amber Commander.lnk'
$ws = New-Object -ComObject WScript.Shell
$sc = $ws.CreateShortcut($lnkPath)
$sc.TargetPath = $exe
$sc.WorkingDirectory = $dir
$sc.Description = 'Amber Commander - fast two-panel file manager'
$sc.IconLocation = "$exe,1"
$sc.Save()
Write-Host "Start menu shortcut: $lnkPath"

# 2) User PATH: `amber` in cmd / PowerShell / Win+R
$userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
if ($null -eq $userPath) { $userPath = '' }
$userPath = [Environment]::ExpandEnvironmentVariables($userPath)
$entries = @($userPath -split ';' | Where-Object { $_ })
if (-not ($entries -contains $dir)) {
    $newPath = ($entries + $dir) -join ';'
    [Environment]::SetEnvironmentVariable('Path', $newPath, 'User')
    Write-Host "Added to user PATH: $dir"
} else {
    Write-Host "Already in user PATH: $dir"
}

# 3) Launch as administrator (AppCompatFlags): UAC prompt on every start.
# To disable: remove the value from
# HKCU\Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers
$ak = 'HKCU:\Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers'
if (-not (Test-Path $ak)) { New-Item -Path $ak -Force | Out-Null }
Set-ItemProperty -Path $ak -Name $exe -Value '~ RUNASADMIN' -Type String
Write-Host "Run as administrator: ON ($exe)"

Write-Host 'Done.'
