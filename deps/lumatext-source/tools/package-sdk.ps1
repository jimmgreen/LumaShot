$ErrorActionPreference='Stop'
function Get-SdkSha256([string]$path) {
  $stream=[System.IO.File]::OpenRead($path)
  $algorithm=[System.Security.Cryptography.SHA256]::Create()
  try { return [BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace('-', '') }
  finally { $algorithm.Dispose(); $stream.Dispose() }
}
$sourceRoot=Split-Path -Parent $PSScriptRoot
foreach($config in @('Debug','Release')){
  $built=Join-Path $sourceRoot "out/build-sdk/$config"
  $binaryName=if($config -eq 'Debug'){'lumatextd'}else{'lumatext'}
  $destination=Join-Path $sourceRoot "out/sdk/$config"
  foreach($part in @('bin','lib','include/lumatext','licenses')){New-Item -ItemType Directory -Path (Join-Path $destination $part) -Force | Out-Null}
  Copy-Item -LiteralPath (Join-Path $built "$binaryName.dll") -Destination (Join-Path $destination "bin/$binaryName.dll")
  Copy-Item -LiteralPath (Join-Path $built "$binaryName.lib") -Destination (Join-Path $destination "lib/$binaryName.lib")
  $pdb=Join-Path $built "$binaryName.pdb";if(Test-Path -LiteralPath $pdb){Copy-Item -LiteralPath $pdb -Destination (Join-Path $destination "bin/$binaryName.pdb")}
  Copy-Item (Join-Path $sourceRoot 'include/lumatext/*') -Destination (Join-Path $destination 'include/lumatext') -Recurse -Force
  Copy-Item (Join-Path $sourceRoot 'LICENSES/*') -Destination (Join-Path $destination 'licenses') -Recurse -Force
  Copy-Item -LiteralPath (Join-Path $sourceRoot 'LICENSE') -Destination (Join-Path $destination 'licenses/LICENSE')
  $manifest=@{configuration=$config;architecture='x64';runtime= $(if($config -eq 'Debug'){'MDd'}else{'MD'});files=@()}
  foreach($file in (Get-ChildItem -LiteralPath $destination -Recurse -File | Where-Object Name -ne 'manifest.json')){
    $manifest.files+=@{path=$file.FullName.Substring($destination.Length+1).Replace('\','/');bytes=$file.Length;sha256=(Get-SdkSha256 $file.FullName)}
  }
  $manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $destination 'manifest.json') -Encoding UTF8
  Write-Output "$config SDK: $destination"
}
Copy-Item -LiteralPath (Join-Path $sourceRoot 'docs/shared-sdk.md') -Destination (Join-Path $sourceRoot 'out/sdk/README.md')
