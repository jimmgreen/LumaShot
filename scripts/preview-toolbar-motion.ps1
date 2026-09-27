param([switch]$SkipRender)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$work = Join-Path $root 'build/toolbar-motion'
New-Item -ItemType Directory -Force -Path $work | Out-Null
if (-not $SkipRender) {
    Push-Location $work
    try { & (Join-Path $root 'build/lumashot_toolbar_motion_test.exe') --preview; if ($LASTEXITCODE -ne 0) { throw 'Toolbar preview failed' } }
    finally { Pop-Location }
}
$frames = @(Get-ChildItem (Join-Path $work 'frames') -Filter 'frame-*.png' | Sort-Object Name)
if ($frames.Count -ne 120) { throw 'Expected 120 native frames' }
Add-Type -AssemblyName System.Drawing
$atlas = New-Object System.Drawing.Bitmap(6480,5000)
try {
    $g = [System.Drawing.Graphics]::FromImage($atlas)
    try {
        for ($i=0; $i -lt $frames.Count; $i++) {
            $im = New-Object System.Drawing.Bitmap($frames[$i].FullName)
            try { $g.DrawImageUnscaled($im,($i % 6)*1080,[int][Math]::Floor($i/6)*250) }
            finally { $im.Dispose() }
        }
    } finally { $g.Dispose() }
    $atlas.Save((Join-Path $work 'toolbar-frames.png'),[System.Drawing.Imaging.ImageFormat]::Png)
} finally { $atlas.Dispose() }
Get-Item (Join-Path $work 'toolbar-frames.png') | Select-Object FullName,Length
