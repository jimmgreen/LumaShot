param(
    [string]$PayloadDir = '',
    [string]$OutputDir = '',
    [string]$IsccPath = '',
    [string]$AppVersion = ''
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
# CMakeLists.txt project(VERSION) is the single source of the release version.
if (-not $AppVersion) { $AppVersion = ([regex]::Match((Get-Content -Raw (Join-Path $root 'CMakeLists.txt')), 'project\(LumaShot VERSION (\d+\.\d+\.\d+)')).Groups[1].Value }
if ($AppVersion -notmatch '^\d+\.\d+\.\d+(\.\d+)?$') { throw "Invalid AppVersion '$AppVersion'" }
if (-not $PayloadDir) { $PayloadDir = Join-Path $root 'dist/LumaShot-setup-payload' }
if (-not $OutputDir) { $OutputDir = Join-Path $root 'dist' }
$PayloadDir = (Resolve-Path -LiteralPath $PayloadDir).Path
foreach ($file in @('LumaShot.exe', 'lumashot_recording_worker.exe', 'lumashot_ocr_worker.exe', 'lumashot_elements_worker.exe',
                    'onnxruntime.dll', 'lumatext.dll', 'vcruntime140.dll', 'vcruntime140_1.dll', 'msvcp140.dll', 'msvcp140_1.dll',
                    'ocr/det.onnx', 'ocr/rec.onnx', 'ocr/dictionary.txt',
                    'ocr/LICENSE', 'ocr/LICENSE-PaddleOCR.txt', 'ocr/ThirdPartyNotices.txt',
                    'NOTICE-VC-Runtime.txt', 'runtime-files.json', 'ffmpeg.exe',
                    'licenses/ffmpeg/LICENSE', 'licenses/ffmpeg/README.txt',
                    'licenses/ffmpeg/BUILD-README.txt', 'licenses/ffmpeg/THIRD-PARTY-NOTICES.txt',
                    'licenses/ffmpeg/manifest.json', 'licenses/ffmpeg/source-lock.json')) {
    if (-not (Test-Path -LiteralPath (Join-Path $PayloadDir $file) -PathType Leaf)) {
        throw "Installer payload is missing: $file. Run scripts/package.ps1 -PackageName LumaShot-setup-payload first."
    }
}
$runtimeMetadata = Get-Content -LiteralPath (Join-Path $PayloadDir 'licenses/ffmpeg/manifest.json') -Raw | ConvertFrom-Json
if ((Get-FileHash -LiteralPath (Join-Path $PayloadDir 'ffmpeg.exe')).Hash -ne $runtimeMetadata.binary.sha256) {
    throw 'Installer MP4 runtime does not match its verified build manifest.'
}
if (-not $IsccPath) {
    $command = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($command) { $IsccPath = $command.Source }
    else {
        $candidates = @(
            (Join-Path $env:LOCALAPPDATA 'Programs/Inno Setup 6/ISCC.exe'),
            (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6/ISCC.exe'),
            (Join-Path $env:ProgramFiles 'Inno Setup 6/ISCC.exe')
        )
        $IsccPath = $candidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
    }
}
if (-not $IsccPath -or -not (Test-Path -LiteralPath $IsccPath -PathType Leaf)) {
    throw 'Inno Setup 6 compiler not found. Install Inno Setup or pass -IsccPath.'
}
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$OutputDir = (Resolve-Path -LiteralPath $OutputDir).Path
& $IsccPath "/DPayloadDir=$PayloadDir" "/DOutputDirPath=$OutputDir" "/DAppVersion=$AppVersion" (Join-Path $PSScriptRoot 'installer.iss')
if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed with exit code $LASTEXITCODE" }
$installer = Join-Path $OutputDir 'LumaShot-Setup.exe'
if (-not (Test-Path -LiteralPath $installer -PathType Leaf)) { throw 'Compiler did not produce the expected installer.' }
Write-Output "Installer: $installer"
Get-FileHash -LiteralPath $installer -Algorithm SHA256