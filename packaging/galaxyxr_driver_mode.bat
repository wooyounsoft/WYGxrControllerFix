<# :
@echo off
rem VD / Steam Link mode switch for the Galaxy XR Steam Link driver (GalaxyXRNative).
rem Usage: galaxyxr_driver_mode.bat [vd | steamlink | status]   (no argument = menu)
powershell -NoProfile -ExecutionPolicy Bypass -Command "& ([scriptblock]::Create((Get-Content -LiteralPath '%~f0' -Encoding UTF8 -Raw))) %*"
pause
exit /b
#>

# Virtual Desktop 에서는 GalaxyXRNative 가 VD 세션에도 개입해서 던지기가 어색해진다 → VD 모드 = 끔, Steam Link 모드 = 켬.
param([string]$Mode = '')
$ErrorActionPreference = 'Stop'

$DriverKey = 'driver_GalaxyXRNative'
$SteamVrProcesses = @('vrmonitor', 'vrserver', 'vrcompositor', 'vrdashboard', 'vrwebhelper', 'vrstartup')
$CloseWaitSeconds = 15
$UTF8NoBom = New-Object System.Text.UTF8Encoding($false)

function Get-SettingsPath {
    $steam = (Get-ItemProperty -Path 'HKCU:\Software\Valve\Steam' -Name SteamPath -ErrorAction SilentlyContinue).SteamPath
    if (-not $steam) { $steam = "${env:ProgramFiles(x86)}\Steam" }
    return (Join-Path $steam 'config\steamvr.vrsettings')
}

function Get-DriverEnabled([string]$json) {
    $m = [regex]::Match($json, '"' + $DriverKey + '"\s*:\s*\{[^{}]*?"enable"\s*:\s*(true|false)')
    if ($m.Success) { return $m.Groups[1].Value -eq 'true' }
    return $null  # 설정에 없음 = SteamVR 기본값 (설치돼 있으면 켜짐)
}

function Set-DriverEnabled([string]$json, [bool]$enable) {
    $value = if ($enable) { 'true' } else { 'false' }
    $withEnable = '("' + $DriverKey + '"\s*:\s*\{[^{}]*?"enable"\s*:\s*)(true|false)'
    if ([regex]::IsMatch($json, $withEnable)) { return [regex]::Replace($json, $withEnable, '${1}' + $value) }
    $block = '("' + $DriverKey + '"\s*:\s*\{)'
    if ([regex]::IsMatch($json, $block)) { return [regex]::Replace($json, $block, '${1} "enable" : ' + $value + ',') }
    # 블록이 없으면 맨 앞에 추가
    return [regex]::Replace($json, '^\s*\{', "{`n   `"$DriverKey`" : {`n      `"enable`" : $value`n   },", 1)
}

function Close-SteamVR {
    $running = Get-Process -Name $SteamVrProcesses -ErrorAction SilentlyContinue
    if (-not $running) { return }
    Write-Host '  SteamVR 을 종료합니다 / closing SteamVR...'
    foreach ($p in $running) { & taskkill.exe /PID $p.Id 2>$null | Out-Null }
    for ($i = 0; $i -lt $CloseWaitSeconds; $i++) {
        if (-not (Get-Process -Name $SteamVrProcesses -ErrorAction SilentlyContinue)) { return }
        Start-Sleep -Seconds 1
    }
    $left = Get-Process -Name $SteamVrProcesses -ErrorAction SilentlyContinue
    if ($left) {
        $answer = Read-Host '  SteamVR 이 종료되지 않습니다. 강제 종료할까요? / force close? (y/n)'
        if ($answer -ne 'y') { throw 'SteamVR 이 실행 중이라 바꿀 수 없습니다 / SteamVR is still running' }
        $left | Stop-Process -Force
        Start-Sleep -Seconds 1
    }
}

function Show-Status([object]$enabled) {
    if ($enabled -eq $false) { Write-Host '  현재 / current: VD 모드 (Galaxy XR 드라이버 꺼짐 / driver OFF)' -ForegroundColor Green }
    elseif ($enabled -eq $true) { Write-Host '  현재 / current: Steam Link 모드 (Galaxy XR 드라이버 켜짐 / driver ON)' -ForegroundColor Cyan }
    else { Write-Host '  현재 / current: 설정 없음 (드라이버가 설치돼 있으면 켜짐) / not set (ON if installed)' -ForegroundColor Yellow }
}

$path = Get-SettingsPath
if (-not (Test-Path -LiteralPath $path)) { throw "SteamVR 설정 파일이 없습니다 / not found: $path" }
$json = [System.IO.File]::ReadAllText($path, $UTF8NoBom)

Write-Host ''
Write-Host '  Galaxy XR 드라이버 모드 전환 / Galaxy XR driver mode (GalaxyXRNative)'
Write-Host "  $path"
Show-Status (Get-DriverEnabled $json)

if (-not $Mode) {
    Write-Host ''
    Write-Host '  1. VD 모드: 드라이버 끔 / Virtual Desktop: driver OFF'
    Write-Host '  2. Steam Link 모드: 드라이버 켬 / Steam Link: driver ON'
    Write-Host '  그 외 / other: 종료 / exit'
    $choice = Read-Host '  선택 / choice'
    $Mode = switch ($choice) { '1' { 'vd' } '2' { 'steamlink' } default { 'status' } }
}

$enable = switch ($Mode.ToLower()) {
    'vd' { $false }
    'steamlink' { $true }
    'status' { return }
    default { throw "알 수 없는 모드 / unknown mode: $Mode (vd | steamlink | status)" }
}

Close-SteamVR
$json = [System.IO.File]::ReadAllText($path, $UTF8NoBom)  # SteamVR 이 종료하면서 다시 썼을 수 있음
$backup = "$path.bak-drivermode"
Copy-Item -LiteralPath $path -Destination $backup -Force
[System.IO.File]::WriteAllText($path, (Set-DriverEnabled $json $enable), $UTF8NoBom)
Write-Host ''
Show-Status (Get-DriverEnabled ([System.IO.File]::ReadAllText($path, $UTF8NoBom)))
Write-Host "  백업 / backup: $backup"
Write-Host '  다음에 SteamVR 을 켜면 적용됩니다 / applies the next time SteamVR starts'
