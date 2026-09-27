$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $PSScriptRoot
$ffmpeg = Join-Path $root 'build/ffmpeg.exe'
$folder = Join-Path $root 'build/mp4-performance'
$source = Join-Path $folder 'source-1080.mp4'
$results = @()
foreach ($config in @(@{preset=6;lp=4},@{preset=8;lp=4})) {
    $name = "p$($config.preset)-lp$($config.lp)"
    $candidate = Join-Path $folder "$name.mp4"
    $clock = [Diagnostics.Stopwatch]::StartNew()
    & $ffmpeg -hide_banner -nostdin -v error -y -threads 4 -i $source -map 0:v:0 -map '0:a:0?' -c:v libsvtav1 -preset $config.preset -crf 36 -svtav1-params "tune=0:lp=$($config.lp):lookahead=16" -pix_fmt yuv420p -fps_mode passthrough -enc_time_base demux -c:a copy -movflags +faststart $candidate 2> (Join-Path $folder "$name-encode.log")
    if ($LASTEXITCODE -ne 0) {throw "Encoding failed $name"}
    $encode = $clock.ElapsedMilliseconds
    $qualityFile = "$name-quality.txt"
    Push-Location $folder
    & $ffmpeg -hide_banner -nostdin -v error -y -xerror -threads 4 -i $source -threads 4 -i $candidate -filter_complex_threads 2 -lavfi "[0:v]settb=AVTB,setpts=N[r];[1:v]settb=AVTB,setpts=N[d];[r][d]ssim=shortest=1:stats_file=$qualityFile" -an -fps_mode passthrough -f null - 2> "$name-validate.log"
    if ($LASTEXITCODE -ne 0) {throw "Validation failed $name"}
    Pop-Location
    $values = @(Get-Content (Join-Path $folder $qualityFile) | ForEach-Object { if ($_ -match 'All:([0-9.]+)') {[double]::Parse($Matches[1],[Globalization.CultureInfo]::InvariantCulture)} })
    $stats = $values | Measure-Object -Average -Minimum
    $results += @{name=$name;encode_ms=$encode;total_ms=$clock.ElapsedMilliseconds;bytes=(Get-Item $candidate).Length;frames=$values.Count;ssim_mean=$stats.Average;ssim_min=$stats.Minimum;below_098=@($values | Where-Object {$_ -lt .98}).Count}
    $results | ConvertTo-Json | Set-Content -Encoding UTF8 (Join-Path $folder 'parameter-results.json')
}
