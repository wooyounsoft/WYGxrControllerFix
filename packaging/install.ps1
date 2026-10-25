# WYGxrControllerFix 설치: 드라이버 폴더를 SteamVR에 등록하고 VDXR(OpenXR) 레이어를 등록한다.
$ErrorActionPreference = 'Stop'

$driverDir = Join-Path $PSScriptRoot 'wygxrcontrollerfix'
$pathsFile = Join-Path $env:LOCALAPPDATA 'openvr\openvrpaths.vrpath'

if ($PSScriptRoot -like "$env:TEMP*") {
    Write-Host '압축 파일 안에서 실행했습니다. 먼저 고정된 폴더(예: C:\Tools\WYGxrControllerFix)에 압축을 푼 뒤 실행하세요.'
    Write-Host 'Run this from an extracted folder (e.g. C:\Tools\WYGxrControllerFix), not from inside the zip.'
    exit 1
}
if (-not (Test-Path (Join-Path $driverDir 'driver.vrdrivermanifest'))) {
    Write-Host "드라이버 폴더를 찾을 수 없습니다 / Driver folder not found: $driverDir"
    exit 1
}
# VDXR(OpenXR) 게임용 레이어 등록 (현재 사용자, 관리자 권한 불필요)
$layerJson = Join-Path $PSScriptRoot 'openxr\XR_APILAYER_WY_gxr_controllerfix.json'
$layerKey = 'HKCU:\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit'
$layerFileName = Split-Path $layerJson -Leaf
if (-not (Test-Path $layerKey)) { New-Item -Path $layerKey -Force | Out-Null }
# 다른 폴더에 등록된 같은 레이어는 두 번 적용되지 않도록 지운다
foreach ($name in (Get-Item $layerKey).GetValueNames()) {
    if ((Split-Path $name -Leaf) -eq $layerFileName -and $name -ne $layerJson) {
        Remove-ItemProperty -Path $layerKey -Name $name
    }
}
New-ItemProperty -Path $layerKey -Name $layerJson -PropertyType DWord -Value 0 -Force | Out-Null
Write-Host "VDXR 레이어 등록 / OpenXR layer registered: $layerJson"

if (Get-Process vrserver -ErrorAction SilentlyContinue) {
    Write-Host 'SteamVR을 먼저 종료하세요 / Please close SteamVR first.'
    exit 1
}
if (-not (Test-Path $pathsFile)) {
    Write-Host 'SteamVR이 설치되어 있지 않거나 한 번도 실행되지 않았습니다 / SteamVR not found.'
    exit 1
}

$runtime = (Get-Content $pathsFile -Raw | ConvertFrom-Json).runtime[0]
$vrpathreg = Join-Path $runtime 'bin\win64\vrpathreg.exe'

# 이전 이름(GxrThrowFix)으로 설치된 드라이버가 있으면 보정이 두 번 적용되므로 등록 해제
$legacyDriverName = 'gxrthrowfix'
foreach ($path in (Get-Content $pathsFile -Raw | ConvertFrom-Json).external_drivers) {
    if ((Split-Path $path -Leaf) -eq $legacyDriverName) {
        & $vrpathreg removedriver $path
        Write-Host "이전 버전(GxrThrowFix) 등록 해제 / Removed old GxrThrowFix: $path"
    }
}

& $vrpathreg adddriver $driverDir

$registered = (Get-Content $pathsFile -Raw | ConvertFrom-Json).external_drivers -contains $driverDir
if (-not $registered) {
    Write-Host '등록 실패 / Registration failed.'
    exit 1
}
Write-Host ''
Write-Host "설치 완료 / Installed: $driverDir"
Write-Host 'SteamVR을 실행하면 자동으로 적용됩니다. 이 폴더를 옮기거나 지우지 마세요.'
Write-Host 'Start SteamVR and it is active. Do not move or delete this folder.'
Write-Host "설정 / Settings: $env:LOCALAPPDATA\WYGxrControllerFix\settings.ini (SteamVR 첫 실행 시 생성 / created on first run)"
