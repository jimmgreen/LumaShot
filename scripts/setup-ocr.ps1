param([switch]$Mirror)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$deps = Join-Path $root 'deps/ocr'
New-Item -ItemType Directory -Force -Path $deps | Out-Null
function Fetch-Verified($url, $destination, $hash) {
    if ((Test-Path -LiteralPath $destination) -and (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -eq $hash) { return }
    & curl.exe -fLsS --connect-timeout 10 --max-time 180 $url -o "$destination.download"
    if ($LASTEXITCODE -ne 0) { throw "Download failed: $url. Model downloads may require -Mirror." }
    if ((Get-FileHash -LiteralPath "$destination.download" -Algorithm SHA256).Hash -ne $hash) { throw "Checksum mismatch: $url" }
    Move-Item -LiteralPath "$destination.download" -Destination $destination -Force
}
$modelHost = if ($Mirror) { 'https://hf-mirror.com' } else { 'https://huggingface.co' }
Fetch-Verified "$modelHost/PaddlePaddle/PP-OCRv6_small_det_onnx/resolve/28fe5895c24fd108c19eb3e8479f4ab385fbfc62/inference.onnx" (Join-Path $deps 'det.onnx') 'D73E0058B7A8086BBD57F3D10B8BCD4FF95363F67E06E2762B5E814FE9C9410E'
Fetch-Verified "$modelHost/PaddlePaddle/PP-OCRv6_small_rec_onnx/resolve/b8f84f0b80c529de40b4fbb3544b84fa7233a513/inference.onnx" (Join-Path $deps 'rec.onnx') '5435FD747C9E0EFE15A96D0B378D5BD157E9492ED8FD80EDF08F30D02FA24634'
$package = Join-Path $deps 'onnxruntime.nupkg'
Fetch-Verified 'https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime/1.22.0/microsoft.ml.onnxruntime.1.22.0.nupkg' $package 'D571E63A2329BAACB713F441E65AD75284DE354DB6E1AC435FE4BEBBB417986A'
& tar -xf $package -C $deps build/native/include runtimes/win-x64/native LICENSE ThirdPartyNotices.txt
if ($LASTEXITCODE -ne 0) { throw 'Cannot extract ONNX Runtime' }
Write-Output 'Offline OCR dependencies verified.'
