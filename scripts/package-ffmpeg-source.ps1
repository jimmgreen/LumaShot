$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$metadataDir = Join-Path $root 'deps/ffmpeg'
$stage = Join-Path $root 'build/ffmpeg-source-package'
$sources = Join-Path $stage 'sources'
New-Item -ItemType Directory -Force -Path $sources | Out-Null
$lock = Get-Content -LiteralPath (Join-Path $metadataDir 'source-lock.json') -Raw | ConvertFrom-Json
foreach ($entry in $lock.PSObject.Properties) {
    $destination = Join-Path $sources ($entry.Name + '.tar.gz')
    $expected = $entry.Value
    if (-not (Test-Path -LiteralPath $destination) -or (Get-FileHash -LiteralPath $destination).Hash -ne $expected.sha256) {
        & curl.exe -fL --retry 3 --connect-timeout 20 --max-time 300 $expected.url -o "$destination.download"
        if ($LASTEXITCODE -ne 0) { throw "Source download failed: $($entry.Name)" }
        if ((Get-FileHash -LiteralPath "$destination.download").Hash -ne $expected.sha256) { throw "Source checksum mismatch: $($entry.Name)" }
        Move-Item -LiteralPath "$destination.download" -Destination $destination -Force
    }
    if ((Get-Item -LiteralPath $destination).Length -ne $expected.bytes) { throw "Source size mismatch: $($entry.Name)" }
    Write-Output "Verified source: $($entry.Name)"
}
foreach ($name in @('LICENSE','README.txt','BUILD-README.txt','THIRD-PARTY-NOTICES.txt','source-lock.json','build-runtime.sh','prepare-sources.py','toolchain-versions.txt','manifest.json')) {
    Copy-Item -LiteralPath (Join-Path $metadataDir $name) -Destination $stage
}
New-Item -ItemType Directory -Force -Path (Join-Path $root 'dist') | Out-Null
$archive = Join-Path $root 'dist/LumaShot-ffmpeg-source.zip'
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $archive -Force
[ordered]@{filename='LumaShot-ffmpeg-source.zip'; bytes=(Get-Item -LiteralPath $archive).Length; sha256=(Get-FileHash -LiteralPath $archive).Hash.ToLowerInvariant()} |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $metadataDir 'source-archive.json') -Encoding UTF8
Get-FileHash -LiteralPath $archive
