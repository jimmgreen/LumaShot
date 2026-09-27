param([ValidatePattern('^[A-Za-z0-9_-]+$')][string]$PackageName = 'LumaShot-ocr')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$output = Join-Path $root "dist/$PackageName"
New-Item -ItemType Directory -Force -Path (Join-Path $output 'ocr') | Out-Null
foreach ($file in @('LumaShot.exe','lumashot_recording_worker.exe','lumashot_ocr_worker.exe','lumashot_elements_worker.exe','onnxruntime.dll','lumatext.dll')) {
    Copy-Item -LiteralPath (Join-Path $root "build/$file") -Destination $output
}
if (-not (Test-Path -LiteralPath (Join-Path $root 'build/ffmpeg.exe'))) { throw 'Missing MP4 optimizer: build/ffmpeg.exe' }
# Fail closed on a reduced runtime that cannot perform the full export validator.
$ffmpegPath = Join-Path $root 'build/ffmpeg.exe'
function Assert-FfmpegComponents {
    param([string]$Category, [string[]]$Required, [string]$RowPattern)
    $listing = (& $ffmpegPath -hide_banner "-$Category" 2>&1 | Out-String)
    if ($LASTEXITCODE -ne 0) { throw "Cannot inspect FFmpeg $Category" }
    $available = @()
    foreach ($line in ($listing -split '\r?\n')) {
        if ($line -match $RowPattern) { $available += $Matches[1] -split ',' }
    }
    foreach ($component in $Required) {
        if ($component -notin $available) { throw "MP4 runtime lacks ${Category}: $component" }
    }
}
Assert-FfmpegComponents encoders @('libsvtav1','libx264','wrapped_avframe') '^\s*[A-Z.]{6}\s+(\S+)'
Assert-FfmpegComponents decoders @('h264','libdav1d','aac') '^\s*[A-Z.]{6}\s+(\S+)'
Assert-FfmpegComponents filters @('ssim','settb','setpts','scale','format','buffer','buffersink') '^\s*[A-Z.]{2,3}\s+(\S+)'
Assert-FfmpegComponents demuxers @('mov') '^\s*[DE ]+\s+(\S+)'
Assert-FfmpegComponents muxers @('mp4','null') '^\s*[DE ]+\s+(\S+)'
Assert-FfmpegComponents protocols @('file','pipe') '^\s*(\w+)\s*$'
$ffmpegManifestPath = Join-Path $root 'deps/ffmpeg/manifest.json'
$ffmpegManifest = Get-Content -LiteralPath $ffmpegManifestPath -Raw | ConvertFrom-Json
if ((Get-FileHash -LiteralPath $ffmpegPath).Hash -ne $ffmpegManifest.binary.sha256 -or
    (Get-Item -LiteralPath $ffmpegPath).Length -ne $ffmpegManifest.binary.bytes) {
    throw 'MP4 runtime differs from its verified source/build manifest'
}
Copy-Item -LiteralPath (Join-Path $root 'build/ffmpeg.exe') -Destination $output
$ffmpegLicense = Join-Path $output 'licenses/ffmpeg'
New-Item -ItemType Directory -Force -Path $ffmpegLicense | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'deps/ffmpeg/LICENSE') -Destination $ffmpegLicense
Copy-Item -LiteralPath (Join-Path $root 'deps/ffmpeg/README.txt') -Destination $ffmpegLicense
Copy-Item -LiteralPath (Join-Path $root 'deps/ffmpeg/BUILD-README.txt') -Destination $ffmpegLicense
foreach ($file in @('manifest.json','source-lock.json','build-runtime.sh','prepare-sources.py','toolchain-versions.txt','THIRD-PARTY-NOTICES.txt','source-archive.json')) {
    Copy-Item -LiteralPath (Join-Path $root "deps/ffmpeg/$file") -Destination $ffmpegLicense
}
if (Test-Path -LiteralPath (Join-Path $root 'build/gifsicle.exe')) {
    Copy-Item -LiteralPath (Join-Path $root 'build/gifsicle.exe') -Destination $output
    $gifLicense = Join-Path $output 'licenses/gifsicle'
    New-Item -ItemType Directory -Force -Path $gifLicense | Out-Null
    Copy-Item -LiteralPath (Join-Path $root 'deps/gifsicle/COPYING') -Destination $gifLicense
    Copy-Item -LiteralPath (Join-Path $root 'deps/gifsicle/README.md') -Destination $gifLicense
    Compress-Archive -Path (Join-Path $root 'deps/gifsicle') -DestinationPath (Join-Path $gifLicense 'source.zip') -Force
}
foreach ($file in @('det.onnx','rec.onnx','dictionary.txt')) {
    Copy-Item -LiteralPath (Join-Path $root "build/ocr/$file") -Destination (Join-Path $output 'ocr')
}
foreach ($file in @('LICENSE','LICENSE-PaddleOCR.txt','ThirdPartyNotices.txt','manifest.json')) {
    Copy-Item -LiteralPath (Join-Path $root "deps/ocr/$file") -Destination (Join-Path $output 'ocr')
}
Copy-Item -LiteralPath (Join-Path $root 'README.md') -Destination $output
Copy-Item -LiteralPath (Join-Path $root 'LICENSE') -Destination $output
Copy-Item -LiteralPath (Join-Path $root 'THIRD-PARTY-NOTICES.md') -Destination $output
New-Item -ItemType Directory -Force -Path (Join-Path $output 'licenses/lexilla') | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'deps/lexilla/License.txt') -Destination (Join-Path $output 'licenses/lexilla/License.txt')
Copy-Item -LiteralPath (Join-Path $root 'deps/scintilla/License.txt') -Destination (Join-Path $output 'licenses/lexilla/Scintilla-License.txt')
Copy-Item -LiteralPath (Join-Path $root 'deps/lexilla-source.json') -Destination (Join-Path $output 'licenses/lexilla/source.json')
New-Item -ItemType Directory -Force -Path (Join-Path $output 'licenses/lumatext') | Out-Null
Copy-Item -Path (Join-Path $root 'deps/lumatext/licenses/*') -Destination (Join-Path $output 'licenses/lumatext')
New-Item -ItemType Directory -Force -Path (Join-Path $output 'docs') | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'docs/pin-selection-tools.md') -Destination (Join-Path $output 'docs')
Copy-Item -LiteralPath (Join-Path $root 'docs/ocr-table.md') -Destination (Join-Path $output 'docs')
Copy-Item -LiteralPath (Join-Path $root 'docs/ocr-runtime-size.md') -Destination (Join-Path $output 'docs')
Copy-Item -LiteralPath (Join-Path $root 'docs/tool-properties.md') -Destination (Join-Path $output 'docs')
Copy-Item -LiteralPath (Join-Path $root 'docs/installer.md') -Destination (Join-Path $output 'docs')
Copy-Item -LiteralPath (Join-Path $root 'docs/mp4-export-compression.md') -Destination (Join-Path $output 'docs')
Copy-Item -LiteralPath (Join-Path $root 'docs/hand-arrow.md') -Destination (Join-Path $output 'docs')
Copy-Item -LiteralPath (Join-Path $root 'docs/hand-arrow-preview.png') -Destination (Join-Path $output 'docs')
Copy-Item -LiteralPath (Join-Path $root 'docs/hand-arrow-fit-preview.png') -Destination (Join-Path $output 'docs')
Copy-Item -LiteralPath (Join-Path $root 'docs/paper-b.md') -Destination (Join-Path $output 'docs')
Copy-Item -LiteralPath (Join-Path $root 'docs/capture-startup.md') -Destination (Join-Path $output 'docs')
Copy-Item -LiteralPath (Join-Path $root 'docs/capture-performance.md') -Destination (Join-Path $output 'docs')
foreach ($file in @('capture-overlap-before.csv','capture-performance-trace.csv','capture-performance-latency.txt','capture-performance-tests.txt')) {
    Copy-Item -LiteralPath (Join-Path $root "docs/$file") -Destination (Join-Path $output 'docs')
}
Copy-Item -LiteralPath (Join-Path $root 'docs/selection-follow.md') -Destination (Join-Path $output 'docs')
foreach ($file in @('unified-toolbar.md','unified-toolbar-tests.txt','reselect-frame-wait-before.csv','reselect-frame-wait-after.csv','reselect-burst-recheck.txt')) {
    Copy-Item -LiteralPath (Join-Path $root "docs/$file") -Destination (Join-Path $output 'docs')
}
foreach ($file in @('selection-real-before.csv','selection-real-after.csv','selection-follow-tests.txt','selection-burst-after.csv')) {
    Copy-Item -LiteralPath (Join-Path $root "docs/$file") -Destination (Join-Path $output 'docs')
}
foreach ($file in @('capture-startup-before.csv','capture-startup-after.csv','magnifier-real-before.csv','magnifier-synthetic-after.csv','magnifier-test-results.txt')) {
    Copy-Item -LiteralPath (Join-Path $root "docs/$file") -Destination (Join-Path $output 'docs')
}
foreach ($file in @('color-picker-verification.md','ocr-verification.md','ocr-test-results.txt','ocr-final-focused-tests.txt','ocr-package-check.txt','ocr-refinement-tests.txt','ocr-speed-before.txt','ocr-speed-after.txt','ocr-dense-comparison.json')) {
    Copy-Item -LiteralPath (Join-Path $root "docs/$file") -Destination (Join-Path $output 'docs')
}
# Include reduced-runtime provenance only when it describes the actual DLL.
$runtimeManifest = Join-Path $root 'deps/ocr-runtime-reduced/manifest.json'
$packagedManifest = Join-Path $output 'ocr/runtime-build.json'
if (Test-Path -LiteralPath $runtimeManifest) {
    $metadata = Get-Content -LiteralPath $runtimeManifest -Raw | ConvertFrom-Json
    if ((Get-FileHash -LiteralPath (Join-Path $output 'onnxruntime.dll')).Hash -eq $metadata.sha256) {
        foreach ($model in @('det.onnx','rec.onnx')) {
            if ((Get-FileHash -LiteralPath (Join-Path $output "ocr/$model")).Hash -ne $metadata.models.$model) {
                throw "Reduced runtime model mismatch: $model"
            }
        }
        Copy-Item -LiteralPath $runtimeManifest -Destination $packagedManifest
    } elseif (Test-Path -LiteralPath $packagedManifest) {
        Remove-Item -LiteralPath $packagedManifest
    }
} elseif (Test-Path -LiteralPath $packagedManifest) {
    Remove-Item -LiteralPath $packagedManifest
}
# ONNX Runtime imports the VC runtime dynamically even though LumaShot itself
# uses /MT. Deploy the redistributable CRT app-locally for clean Windows systems.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Cannot locate the Visual C++ redistributable directory' }
$versions = Get-ChildItem -LiteralPath (Join-Path $vs 'VC/Redist/MSVC') -Directory | Where-Object Name -Match '^\d+\.\d+\.\d+$' | Sort-Object { [version]$_.Name } -Descending
$crt = Get-ChildItem -Path (Join-Path $versions[0].FullName 'x64/Microsoft.VC*.CRT') -Directory | Select-Object -First 1
if (-not $crt) { throw 'Cannot locate x64 CRT files' }
# Only deploy the transitive CRT imports used by our shipped binaries.
$requiredCrt = @('msvcp140.dll','msvcp140_1.dll','vcruntime140.dll','vcruntime140_1.dll')
foreach ($name in $requiredCrt) {
    Copy-Item -LiteralPath (Join-Path $crt.FullName $name) -Destination $output
}
# Remove only obsolete CRT files from older payloads when reusing this directory.
$resolvedOutput = (Resolve-Path -LiteralPath $output).Path
foreach ($file in (Get-ChildItem -LiteralPath $crt.FullName -Filter '*.dll')) {
    if ($file.Name -notin $requiredCrt) {
        $obsolete = Join-Path $resolvedOutput $file.Name
        if (Test-Path -LiteralPath $obsolete -PathType Leaf) {
            Remove-Item -LiteralPath $obsolete
        }
    }
}
# Validate direct and delay imports of every shipped module, including CRT DLLs.
# Fail future packaging if a new binary needs an additional redistributable.
$toolset = Get-ChildItem -LiteralPath (Join-Path $vs 'VC/Tools/MSVC') -Directory | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
$dumpbin = Join-Path $toolset.FullName 'bin/Hostx64/x64/dumpbin.exe'
foreach ($module in (Get-ChildItem -LiteralPath $output -File | Where-Object Extension -In @('.exe','.dll'))) {
    $imports = & $dumpbin /NOLOGO /DEPENDENTS $module.FullName
    if ($LASTEXITCODE -ne 0) { throw "Cannot inspect imports: $($module.Name)" }
    foreach ($line in $imports) {
        if ($line -match '^\s+((?:msvc[pr]|vcruntime|concrt|vccorlib|vcomp)[A-Za-z0-9_]*\.dll)\s*$') {
            $dependency = $Matches[1]
            # msvcrt.dll is the Windows system CRT, not an app-local VC redist.
            if ($dependency -ieq 'msvcrt.dll') { continue }
            if (-not (Test-Path -LiteralPath (Join-Path $output $dependency) -PathType Leaf)) {
                throw "Missing runtime dependency: $($module.Name) -> $dependency"
            }
        }
    }
}
Get-ChildItem -LiteralPath $output -Filter '*.dll' | ForEach-Object {
    [pscustomobject]@{ file=$_.Name; version=$_.VersionInfo.FileVersion; sha256=(Get-FileHash -LiteralPath $_.FullName).Hash }
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output 'runtime-files.json') -Encoding UTF8
$notice = 'Microsoft Visual C++ Runtime Libraries. Copyright Microsoft Corporation. App-local redistributable files from the installed Visual Studio VC/Redist/MSVC x64 CRT. Redistribution is governed by the Visual Studio software license and its redistributable code list. https://learn.microsoft.com/cpp/windows/redistributing-visual-cpp-files'
Set-Content -LiteralPath (Join-Path $output 'NOTICE-VC-Runtime.txt') -Value $notice -Encoding UTF8
$bytes = (Get-ChildItem -LiteralPath $output -File -Recurse | Measure-Object -Property Length -Sum).Sum
Write-Output "Portable package: $output"
Write-Output ('Uncompressed size: {0:N2} MiB' -f ($bytes / 1MB))
$archive = Join-Path $root "dist/$PackageName.zip"
Compress-Archive -Path (Join-Path $output '*') -DestinationPath $archive -Force
Write-Output "Archive: $archive"
