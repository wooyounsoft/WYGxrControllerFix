# WYGxrControllerFix 관리 도구: 설치(등록) 상태, 마지막 실행에서 보정이 켜졌는지 보여 주고 보정을 켜고 끈다.
$ErrorActionPreference = 'SilentlyContinue'

$LayerFileName = 'XR_APILAYER_WY_gxr_controllerfix.json'
$LayerKey = 'HKCU:\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit'
$DriverName = 'wygxrcontrollerfix'
$DataDir = Join-Path $env:LOCALAPPDATA 'WYGxrControllerFix'
$LayerLog = Join-Path $DataDir 'openxr_layer.log'
$SettingsFile = Join-Path $DataDir 'settings.ini'
$PathsFile = Join-Path $env:LOCALAPPDATA 'openvr\openvrpaths.vrpath'
$LogTag = '[WYGxrControllerFix]'
$FixOn = 'fix ON'
$FixOff = 'fix OFF'

# 메뉴에서 바꿀 수 있는 설정 (키, 기본값, 설명)
# Params: 값 설정 (키, 기본값, 이름, 최소, 최대)
$Toggles = [ordered]@{
    '1' = @{ Key = 'enabled';        Default = '1'; Name = '보정 전체 / all fixes' }
    '2' = @{ Key = 'openxrEnabled';  Default = '1'; Name = 'VDXR 게임 보정 / VDXR games' }
    '3' = @{ Key = 'releaseBoost';   Default = '1'; Name = '놓을 때 속도 보정 / release boost' }
    '4' = @{ Key = 'poseLagFix';     Default = '1'; Name = '손 위치 정확도 (SteamVR) / hand position accuracy (SteamVR)' }
    '5' = @{ Key = 'hapticBoost';    Default = '0'; Name = '진동 강화 / stronger haptics'
             Params = @(@{ Key = 'hapticGain'; Default = '1.5'; Name = '세기 배율 / gain'; Min = 1.0; Max = 4.0 },
                        @{ Key = 'hapticMinMs'; Default = '20'; Name = '최소 길이 ms / min length ms'; Min = 0; Max = 100 }) }
    '6' = @{ Key = 'angleOffset';    Default = '0'; Name = '컨트롤러 각도 조정 / controller angle'
             Params = @(@{ Key = 'angleOffsetDeg'; Default = '0'; Name = '각도 (+ = 앞쪽 위로) / degrees (+ = tip up)'; Min = -45; Max = 45 }) }
    '7' = @{ Key = 'stickTuning';    Default = '0'; Name = '스틱 데드존/감도 / thumbstick deadzone/curve'
             Params = @(@{ Key = 'stickDeadzone'; Default = '0.10'; Name = '데드존 / deadzone'; Min = 0; Max = 0.5 },
                        @{ Key = 'stickCurve'; Default = '1.0'; Name = '감도 곡선 (1 = 그대로) / curve (1 = linear)'; Min = 0.5; Max = 3.0 }) }
}

function Show([bool]$ok, [string]$text) {
    if ($ok) { Write-Host "  [OK]  $text" -ForegroundColor Green }
    else { Write-Host "  [!!]  $text" -ForegroundColor Yellow }
}

function Get-Setting([string]$key, [string]$default) {
    if (-not (Test-Path $SettingsFile)) { return $default }
    $line = Get-Content $SettingsFile -Encoding UTF8 | Where-Object { $_ -match "^\s*$key\s*=" } | Select-Object -First 1
    if ($line) { return ($line -split '=', 2)[1].Trim() }
    return $default
}

# key= 줄만 바꾸고 나머지는 그대로 둔다. 게임 실행 중이면 1초 안에 반영된다.
function Set-Setting([string]$key, [string]$value) {
    $lines = @()
    if (Test-Path $SettingsFile) { $lines = @(Get-Content $SettingsFile -Encoding UTF8) }
    else { New-Item -ItemType Directory -Force $DataDir | Out-Null }
    $found = $false
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match "^\s*$key\s*=") { $lines[$i] = "$key=$value"; $found = $true; break }
    }
    if (-not $found) { $lines += "$key=$value" }
    [IO.File]::WriteAllLines($SettingsFile, [string[]]$lines, (New-Object Text.UTF8Encoding $false))
}

function LastVerdicts([string[]]$lines) {
    # 손/장치별 마지막 판정만 보여 준다
    $last = [ordered]@{}
    foreach ($line in $lines) {
        if (-not ($line.Contains($FixOn) -or $line.Contains($FixOff))) { continue }
        if ($line -match '(device \d+|left hand|right hand)') { $last[$Matches[1]] = $line }
    }
    return $last.Values
}

function Show-VelocityUse([string[]]$lines) {
    # 레이어는 30초마다 stats 줄을 남긴다. 게임이 속도를 한 번도 요청하지 않았으면 손 위치로 직접 계산하는 게임.
    # 직접 계산하는 게임이면 $true (보정 판정이 나지 않는 게 정상)
    $stats = $lines | Where-Object { $_ -match 'locates=(\d+) velocityRequested=(\d+)' } | Select-Object -Last 1
    if (-not $stats -or -not ($stats -match 'locates=(\d+) velocityRequested=(\d+)') -or [int64]$Matches[1] -eq 0) {
        Write-Host '        던지기 방식: 아직 모름 (30초 이상 플레이 후 확인) / throw method: unknown yet (play 30 s+)'
        return $false
    }
    if ([int64]$Matches[2] -eq 0) {
        Write-Host '        던지기 방식: 손 위치로 직접 계산 → 원래도 잘 되는 게임, 보정 영향 없음' -ForegroundColor Green
        Write-Host '                     throw method: computed from hand movement -> works anyway, fix not involved'
        return $true
    }
    Write-Host '        던지기 방식: 런타임 속도 사용 → 보정이 적용되는 게임' -ForegroundColor Yellow
    Write-Host '                     throw method: uses runtime velocity -> the fix applies to this game'
    return $false
}

function Show-Status {
    Write-Host ''
    Write-Host 'WYGxrControllerFix 관리 / manager' -ForegroundColor Cyan

    # SteamVR 애드온
    Write-Host ''
    Write-Host 'SteamVR 게임 / SteamVR games' -ForegroundColor Cyan
    $paths = $null
    if (Test-Path $PathsFile) { $paths = Get-Content $PathsFile -Raw | ConvertFrom-Json }
    $driver = $paths.external_drivers | Where-Object { (Split-Path $_ -Leaf) -eq $DriverName } | Select-Object -First 1
    Show ($null -ne $driver -and (Test-Path $driver)) ("애드온 등록 / add-on registered: " + $(if ($driver) { $driver } else { '없음 / not registered (install.bat)' }))

    $vrLog = $null
    if ($paths.log) { $vrLog = Join-Path $paths.log[0] 'vrserver.txt' }
    if ($vrLog -and (Test-Path $vrLog)) {
        $lines = @(Select-String -Path $vrLog -SimpleMatch $LogTag | ForEach-Object { $_.Line })
        # 마지막 실행분만 (애드온은 시작할 때 settings: 줄을 남긴다)
        $sessionStart = -1
        for ($i = $lines.Count - 1; $i -ge 0; $i--) { if ($lines[$i].Contains("$LogTag settings:")) { $sessionStart = $i; break } }
        if ($sessionStart -gt 0) { $lines = $lines[$sessionStart..($lines.Count - 1)] }
        if ($lines) {
            $verdicts = LastVerdicts $lines
            if ($verdicts) {
                Write-Host '  마지막 SteamVR 실행 / last SteamVR session:'
                foreach ($v in $verdicts) { Write-Host "        $($v.Substring($v.IndexOf($LogTag) + $LogTag.Length).Trim())" }
            } else {
                Write-Host '  마지막 SteamVR 실행: 아직 판정 전 (컨트롤러를 몇 번 휘둘러 보세요) / not decided yet'
            }
        } else {
            Write-Host '  마지막 SteamVR 실행에서 애드온이 로드되지 않았습니다 / add-on was not loaded in the last SteamVR session'
        }
    }

    # VDXR 레이어
    Write-Host ''
    Write-Host 'VDXR 게임 / VDXR games' -ForegroundColor Cyan
    $layer = $null
    if (Test-Path $LayerKey) {
        $key = Get-Item $LayerKey
        foreach ($name in $key.GetValueNames()) {
            if ((Split-Path $name -Leaf) -eq $LayerFileName) { $layer = $name; $layerOn = ($key.GetValue($name) -eq 0) }
        }
    }
    Show ($null -ne $layer -and $layerOn -and (Test-Path $layer)) ("레이어 등록 / layer registered: " + $(if ($layer) { $layer } else { '없음 / not registered (install.bat)' }))

    if (Test-Path $LayerLog) {
        $lines = Get-Content $LayerLog
        $start = -1
        for ($i = $lines.Count - 1; $i -ge 0; $i--) { if ($lines[$i] -match "app='") { $start = $i; break } }
        if ($start -ge 0) {
            Write-Host '  마지막 VDXR 게임 / last VDXR game:'
            Write-Host "        $($lines[$start])"
            $verdicts = LastVerdicts $lines[$start..($lines.Count - 1)]
            $selfComputed = Show-VelocityUse $lines[$start..($lines.Count - 1)]
            if ($verdicts) { foreach ($v in $verdicts) { Write-Host "        $v" } }
            elseif (-not $selfComputed) { Write-Host '        아직 판정 전 (컨트롤러를 몇 번 휘둘러 보세요) / not decided yet' }
        }
    } else {
        Write-Host '  아직 VDXR 게임 실행 기록이 없습니다 / no VDXR game run yet'
    }
    Write-Host ''
    Write-Host "'GXR detected -> fix ON' = 보정 적용 중 / fix active" -ForegroundColor Cyan
    Write-Host "'not needed -> fix OFF'  = 보정이 필요 없는 기기 (Quest, Steam Link) / not needed"
    Write-Host ''
}

function Show-Menu {
    Write-Host '설정 / settings' -ForegroundColor Cyan
    foreach ($k in $Toggles.Keys) {
        $t = $Toggles[$k]
        $on = (Get-Setting $t.Key $t.Default) -ne '0'
        $state = if ($on) { '켜짐 / ON ' } else { '꺼짐 / OFF' }
        $values = ''
        if ($t['Params']) { $values = '  (' + (($t['Params'] | ForEach-Object { "$($_.Key)=$(Get-Setting $_.Key $_.Default)" }) -join ', ') + ')' }
        Write-Host ("  {0}. [{1}]  {2}{3}" -f $k, $state, $t.Name, $values) -ForegroundColor $(if ($on) { 'Green' } else { 'Yellow' })
    }
    Write-Host ''
    Write-Host '  번호 = 켜기/끄기 (값이 있는 항목은 값도 바꿀 수 있음, 게임 중에도 1초 안에 반영)'
    Write-Host '  number = toggle / change values (applies within 1 s, even in game)'
    Write-Host '  Enter = 새로고침 / refresh,  Q = 종료 / quit'
}

function Toggle-Item($t) {
    $on = (Get-Setting $t.Key $t.Default) -ne '0'
    Set-Setting $t.Key $(if ($on) { '0' } else { '1' })
}

# 값이 있는 항목: 켜기/끄기 또는 값 바꾸기
function Edit-Item($t) {
    Write-Host ''
    Write-Host "  $($t.Name)" -ForegroundColor Cyan
    Write-Host '  T. 켜기/끄기 / toggle'
    for ($i = 0; $i -lt $t['Params'].Count; $i++) {
        $v = $t['Params'][$i]
        Write-Host ("  {0}. {1} = {2}  ({3} ~ {4})" -f ($i + 1), $v.Name, (Get-Setting $v.Key $v.Default), $v.Min, $v.Max)
    }
    $sub = (Read-Host '선택 / choice').Trim().ToUpper()
    if ($sub -eq 'T') { Toggle-Item $t; return }
    $index = 0
    if (-not [int]::TryParse($sub, [ref]$index) -or $index -lt 1 -or $index -gt $t['Params'].Count) { return }
    $v = $t['Params'][$index - 1]
    $text = (Read-Host "$($v.Name) ($($v.Min) ~ $($v.Max))").Trim()
    $number = 0.0
    if (-not [double]::TryParse($text, [Globalization.NumberStyles]::Float, [Globalization.CultureInfo]::InvariantCulture, [ref]$number) -or
        $number -lt $v.Min -or $number -gt $v.Max) {
        Write-Host "  범위 밖이거나 숫자가 아닙니다 / out of range" -ForegroundColor Yellow
        Start-Sleep -Seconds 1
        return
    }
    Set-Setting $v.Key $number.ToString([Globalization.CultureInfo]::InvariantCulture)
    Set-Setting $t.Key '1'  # 값을 바꾸면 켠다
}

while ($true) {
    Clear-Host
    Show-Status
    Show-Menu
    $choice = (Read-Host '선택 / choice').Trim().ToUpper()
    if ($choice -eq 'Q') { break }
    if ($Toggles.Contains($choice)) {
        $t = $Toggles[$choice]
        if ($t['Params']) { Edit-Item $t } else { Toggle-Item $t }
    }
}
