# WYGxrControllerFix 제거: SteamVR 등록과 VDXR(OpenXR) 레이어 등록을 해제한다.
$ErrorActionPreference = 'Stop'

$driverDir = Join-Path $PSScriptRoot 'wygxrcontrollerfix'
$pathsFile = Join-Path $env:LOCALAPPDATA 'openvr\openvrpaths.vrpath'

$layerKey = 'HKCU:\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit'
$layerFileName = 'XR_APILAYER_WY_gxr_controllerfix.json'
if (Test-Path $layerKey) {
    foreach ($name in (Get-Item $layerKey).GetValueNames()) {
        if ((Split-Path $name -Leaf) -eq $layerFileName) { Remove-ItemProperty -Path $layerKey -Name $name }
    }
}
Write-Host 'VDXR 레이어 등록 해제 / OpenXR layer removed'

if (Get-Process vrserver -ErrorAction SilentlyContinue) {
    Write-Host 'SteamVR을 먼저 종료하세요 / Please close SteamVR first.'
    exit 1
}
if (-not (Test-Path $pathsFile)) {
    Write-Host 'SteamVR not found.'
    exit 1
}

$runtime = (Get-Content $pathsFile -Raw | ConvertFrom-Json).runtime[0]
$vrpathreg = Join-Path $runtime 'bin\win64\vrpathreg.exe'
& $vrpathreg removedriver $driverDir

Write-Host ''
Write-Host '제거 완료 / Uninstalled. 이제 이 폴더를 지워도 됩니다 / You can now delete this folder.'
Write-Host "설정 파일은 남겨 두었습니다 / Settings kept at: $env:LOCALAPPDATA\WYGxrControllerFix"
