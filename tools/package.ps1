# 배포 패키지 생성: Release 빌드 → dist\WYGxrControllerFix-v<버전>.zip
param([string]$Version = '0.1.0')
$ErrorActionPreference = 'Stop'

$root = Split-Path $PSScriptRoot -Parent
$name = "WYGxrControllerFix-v$Version"
$stage = Join-Path $root "dist\$name"

# 가상 패드 드라이버 설치 파일 (D-pad, install.bat 이 없을 때만 설치)
$vigemBusFile = 'ViGEmBus_1.22.0_x64_x86_arm64.exe'
$vigemBusUrl = "https://github.com/nefarius/ViGEmBus/releases/download/v1.22.0/$vigemBusFile"
$vigemBusSha256 = '89220A7865076B342892F98865F3499FB7C4CFD673159E89D352C360FD014C6A'
$vigemBusPath = Join-Path $root "third_party\vigembus\$vigemBusFile"
if (-not (Test-Path $vigemBusPath)) { Invoke-WebRequest $vigemBusUrl -OutFile $vigemBusPath -UseBasicParsing }
if ((Get-FileHash $vigemBusPath -Algorithm SHA256).Hash -ne $vigemBusSha256) { throw 'ViGEmBus installer hash mismatch' }

cmake --build (Join-Path $root 'build') --config Release
if ($LASTEXITCODE -ne 0) { throw 'build failed' }
& (Join-Path $root 'build\Release\EstimatorTest.exe')
if ($LASTEXITCODE -ne 0) { throw 'tests failed' }

if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force (Join-Path $stage 'licenses') | Out-Null
Copy-Item (Join-Path $root 'deploy\wygxrcontrollerfix') (Join-Path $stage 'wygxrcontrollerfix') -Recurse
Copy-Item (Join-Path $root 'deploy\openxr') (Join-Path $stage 'openxr') -Recurse
Get-ChildItem (Join-Path $stage 'openxr') -Exclude '*.dll', '*.json' | Remove-Item -Recurse -Force
Copy-Item (Join-Path $root 'packaging\*') $stage
Copy-Item (Join-Path $root 'LICENSE') (Join-Path $stage 'LICENSE.txt')
Copy-Item (Join-Path $root 'build\_deps\minhook-src\LICENSE.txt') (Join-Path $stage 'licenses\MinHook-LICENSE.txt')
Copy-Item (Join-Path $root 'build\_deps\vigemclient-src\LICENSE') (Join-Path $stage 'licenses\ViGEmClient-LICENSE.txt')
Copy-Item $vigemBusPath $stage
Copy-Item (Join-Path $root 'third_party\vigembus\LICENSE') (Join-Path $stage 'licenses\ViGEmBus-LICENSE.txt')
Copy-Item (Join-Path $root 'third_party\openvr\LICENSE') (Join-Path $stage 'licenses\OpenVR-LICENSE.txt')
Copy-Item (Join-Path $root 'third_party\openxr\LICENSE') (Join-Path $stage 'licenses\OpenXR-LICENSE.txt')

$zip = Join-Path $root "dist\$name.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path $stage -DestinationPath $zip
Write-Host "package: $zip"
