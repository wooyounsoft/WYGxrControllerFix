# 배포 패키지 생성: Release 빌드 → dist\WYGxrControllerFix-v<버전>.zip
param([string]$Version = '0.1.0')
$ErrorActionPreference = 'Stop'

$root = Split-Path $PSScriptRoot -Parent
$name = "WYGxrControllerFix-v$Version"
$stage = Join-Path $root "dist\$name"

cmake --build (Join-Path $root 'build') --config Release
if ($LASTEXITCODE -ne 0) { throw 'build failed' }
& (Join-Path $root 'build\Release\EstimatorTest.exe')
if ($LASTEXITCODE -ne 0) { throw 'tests failed' }

if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force (Join-Path $stage 'licenses') | Out-Null
Copy-Item (Join-Path $root 'deploy\wygxrcontrollerfix') (Join-Path $stage 'wygxrcontrollerfix') -Recurse
Copy-Item (Join-Path $root 'packaging\*') $stage
Copy-Item (Join-Path $root 'LICENSE') (Join-Path $stage 'LICENSE.txt')
Copy-Item (Join-Path $root 'build\_deps\minhook-src\LICENSE.txt') (Join-Path $stage 'licenses\MinHook-LICENSE.txt')
Copy-Item (Join-Path $root 'third_party\openvr\LICENSE') (Join-Path $stage 'licenses\OpenVR-LICENSE.txt')

$zip = Join-Path $root "dist\$name.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path $stage -DestinationPath $zip
Write-Host "package: $zip"
