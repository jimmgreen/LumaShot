param(
    [Parameter(Mandatory=$true)][string]$RuntimePath,
    [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Label = 'candidate'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$RuntimePath = (Resolve-Path -LiteralPath $RuntimePath).Path
$staging = Join-Path $root "build/runtime-verification-$Label"
$bin = Join-Path $staging 'build'
New-Item -ItemType Directory -Force -Path (Join-Path $bin 'ocr') | Out-Null
foreach ($name in @('lumashot_table_test.exe','lumashot_ocr_service_test.exe','lumashot_ocr_worker.exe','lumatext.dll')) {
    Copy-Item -LiteralPath (Join-Path $root "build/$name") -Destination $bin
}
foreach ($name in @('msvcp140.dll','msvcp140_1.dll','vcruntime140.dll','vcruntime140_1.dll')) {
    Copy-Item -LiteralPath (Join-Path $root "dist/LumaShot-setup-payload/$name") -Destination $bin
}
foreach ($name in @('det.onnx','rec.onnx','dictionary.txt')) {
    Copy-Item -LiteralPath (Join-Path $root "build/ocr/$name") -Destination (Join-Path $bin 'ocr')
}
Copy-Item -LiteralPath $RuntimePath -Destination (Join-Path $bin 'onnxruntime.dll')
function Run-Check([string]$Name, [string]$Arguments = '') {
    $parameters = @{
        FilePath = (Join-Path $bin "$Name.exe")
        WorkingDirectory = $staging
        WindowStyle = 'Hidden'
        RedirectStandardOutput = (Join-Path $staging "$Name.txt")
        RedirectStandardError = (Join-Path $staging "$Name-errors.txt")
        PassThru = $true
        Wait = $true
    }
    if ($Arguments) { $parameters.ArgumentList = $Arguments }
    $process = Start-Process @parameters
    Get-Content -LiteralPath $parameters.RedirectStandardOutput -Tail 8
    if ($process.ExitCode -ne 0) {
        Get-Content -LiteralPath $parameters.RedirectStandardError -Tail 15
        throw "$Name failed: exit $($process.ExitCode)"
    }
}
Run-Check 'lumashot_table_test' '--model'
Run-Check 'lumashot_ocr_service_test'
Get-FileHash -LiteralPath (Join-Path $bin 'onnxruntime.dll')
Write-Output "Runtime verification passed: $staging"
