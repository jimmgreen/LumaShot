param(
  [Parameter(Mandatory = $true)]
  [string]$InstallDirectory,
  [string]$ConsumerExecutable,
  [int64]$MaximumRuntimeBytes = 15728640
)

$ErrorActionPreference = 'Stop'
$install = (Resolve-Path -LiteralPath $InstallDirectory).Path
$dll = Join-Path $install 'bin\lumatext.dll'
if (!(Test-Path -LiteralPath $dll)) {
  $dll = Join-Path $install 'bin\lumatextd.dll'
}
if (!(Test-Path -LiteralPath $dll)) {
  throw "LumaText DLL was not installed under $install\bin"
}

$icuFiles = Get-ChildItem -LiteralPath $install -Recurse -File |
  Where-Object { $_.Name -match '^icu(uc|dt).*\.dll$' }
if ($icuFiles) {
  throw "ICU runtime files were installed: $($icuFiles.FullName -join ', ')"
}

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (!(Test-Path -LiteralPath $vswhere)) { throw 'vswhere.exe was not found' }
$visualStudio = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$dumpbin = Get-ChildItem -Path "$visualStudio\VC\Tools\MSVC\*\bin\Hostx64\x64\dumpbin.exe" |
  Sort-Object FullName -Descending | Select-Object -First 1
if (!$dumpbin) { throw 'dumpbin.exe was not found' }
$dependencies = & $dumpbin.FullName /DEPENDENTS $dll 2>&1 | Out-String
if ($dependencies -match '(?i)icu(uc|dt)\d+\.dll') {
  throw "LumaText imports ICU:`n$dependencies"
}

$runtimeBytes = (Get-Item -LiteralPath $dll).Length
if ($runtimeBytes -gt $MaximumRuntimeBytes) {
  throw "Runtime is $runtimeBytes bytes; limit is $MaximumRuntimeBytes bytes"
}

if ($ConsumerExecutable) {
  $consumer = (Resolve-Path -LiteralPath $ConsumerExecutable).Path
  $isolation = Join-Path ([IO.Path]::GetTempPath()) "lumatext-package-$([guid]::NewGuid())"
  try {
    New-Item -ItemType Directory -Path $isolation | Out-Null
    Copy-Item -LiteralPath $dll -Destination $isolation
    Copy-Item -LiteralPath $consumer -Destination $isolation
    $isolatedConsumer = Join-Path $isolation (Split-Path -Leaf $consumer)
    $process = Start-Process -FilePath $isolatedConsumer -WorkingDirectory $isolation `
      -WindowStyle Hidden -Wait -PassThru
    if ($process.ExitCode -ne 0) {
      throw "Isolated shared consumer exited with $($process.ExitCode)"
    }
  } finally {
    if (Test-Path -LiteralPath $isolation) {
      Remove-Item -LiteralPath $isolation -Recurse -Force
    }
  }
}
Write-Host "Verified ICU-free runtime: $dll ($runtimeBytes bytes)"
