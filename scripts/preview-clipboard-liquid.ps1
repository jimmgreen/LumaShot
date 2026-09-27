param([switch]$SkipRender,[switch]$Detach)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$work = Join-Path $root 'build/clipboard-liquid'
$test = Join-Path $root 'build/lumashot_clipboard_liquid_motion_test.exe'
New-Item -ItemType Directory -Force -Path $work | Out-Null
if (-not $SkipRender) {
    Push-Location $work
    try { $mode = if ($Detach) { '--detach-preview' } else { '--preview' }; & $test $mode; if ($LASTEXITCODE -ne 0) { throw "Synthetic renderer failed: $LASTEXITCODE" } }
    finally { Pop-Location }
}
$frames = @(Get-ChildItem -LiteralPath (Join-Path $work 'frames') -Filter 'frame-*.png' | Sort-Object Name)
if ($frames.Count -ne 150) { throw "Expected 150 synthetic frames, got $($frames.Count)" }
# Lossless atlas: 10 columns x 15 rows, row-major, 720 x 780 pixels per frame,
# 30 fps. No additional runtime decoder, dependency, desktop capture or input.
Add-Type -AssemblyName System.Drawing
$output = Join-Path $work 'liquid-frames.png'
$atlas = New-Object System.Drawing.Bitmap(7200,11700,[System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
try {
    $graphics = [System.Drawing.Graphics]::FromImage($atlas)
    try {
        $graphics.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
        for ($i=0; $i -lt $frames.Count; $i++) {
            $bitmap = New-Object System.Drawing.Bitmap($frames[$i].FullName)
            try {
                if ($bitmap.Width -ne 720 -or $bitmap.Height -ne 780) { throw 'Unexpected fixture size' }
                $graphics.DrawImageUnscaled($bitmap,($i % 10)*720,[int][Math]::Floor($i/10)*780)
            } finally { $bitmap.Dispose() }
        }
    } finally { $graphics.Dispose() }
    $atlas.Save($output,[System.Drawing.Imaging.ImageFormat]::Png)
} finally { $atlas.Dispose() }
Get-Item -LiteralPath $output | Select-Object FullName,Length
Get-FileHash -LiteralPath $output -Algorithm SHA256
