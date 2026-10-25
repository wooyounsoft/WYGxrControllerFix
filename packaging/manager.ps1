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
$Toggles = [ordered]@{
    '1' = @{ Key = 'enabled';       Default = '1'; Name = '보정 전체 / all fixes' }
    '2' = @{ Key = 'openxrEnabled'; Default = '1'; Name = 'VDXR 게임 보정 / VDXR games' }
    '3' = @{ Key = 'releaseBoost';  Default = '1'; Name = '놓을 때 속도 보정 / release boost' }
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
            if ($verdicts) { foreach ($v in $verdicts) { Write-Host "        $v" } }
            else { Write-Host '        아직 판정 전 (컨트롤러를 몇 번 휘둘러 보세요) / not decided yet' }
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
        Write-Host ("  {0}. [{1}]  {2}" -f $k, $state, $t.Name) -ForegroundColor $(if ($on) { 'Green' } else { 'Yellow' })
    }
    Write-Host ''
    Write-Host '  번호 = 켜기/끄기 전환 (게임 중에도 1초 안에 반영) / number = toggle (applies within 1 s)'
    Write-Host '  Enter = 새로고침 / refresh,  Q = 종료 / quit'
}

while ($true) {
    Clear-Host
    Show-Status
    Show-Menu
    $choice = (Read-Host '선택 / choice').Trim().ToUpper()
    if ($choice -eq 'Q') { break }
    if ($Toggles.Contains($choice)) {
        $t = $Toggles[$choice]
        $on = (Get-Setting $t.Key $t.Default) -ne '0'
        Set-Setting $t.Key $(if ($on) { '0' } else { '1' })
    }
}
