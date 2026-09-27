$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$testRoot = Join-Path $root 'build/installer-upgrade-test'
$payload = Join-Path $testRoot 'payload'
$install = Join-Path $testRoot 'installed'
New-Item -ItemType Directory -Force -Path $payload,$install | Out-Null
$source = @'
using System;
using System.IO;
using System.Runtime.InteropServices;
public class UpgradeFixture {
 [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)] struct WC {public uint style;public Proc proc;public int cls,win;public IntPtr instance,icon,cursor,brush;public string menu,name;}
 [StructLayout(LayoutKind.Sequential)] struct MSG {public IntPtr window;public uint message;public UIntPtr wp;public IntPtr lp;public uint time;public int x,y;public uint extra;}
 delegate IntPtr Proc(IntPtr w,uint m,UIntPtr p,IntPtr l);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern ushort RegisterClassW(ref WC c);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern IntPtr CreateWindowExW(uint ex,string cls,string title,uint style,int x,int y,int width,int height,IntPtr parent,IntPtr menu,IntPtr instance,IntPtr param);
 [DllImport("user32.dll")] static extern int GetMessageW(out MSG msg,IntPtr window,uint min,uint max);
 [DllImport("user32.dll")] static extern IntPtr DispatchMessageW(ref MSG msg);
 [DllImport("user32.dll")] static extern IntPtr DefWindowProcW(IntPtr w,uint m,UIntPtr p,IntPtr l);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode)] static extern IntPtr GetModuleHandleW(string name);
 [DllImport("user32.dll")] static extern void PostQuitMessage(int code);
 static string folder=AppDomain.CurrentDomain.BaseDirectory;
 static Proc callback=Handle;
 static IntPtr Handle(IntPtr w,uint m,UIntPtr p,IntPtr l){if(m==0x10){File.AppendAllText(Path.Combine(folder,"lifecycle.txt"),"closed\n");PostQuitMessage(0);return IntPtr.Zero;}return DefWindowProcW(w,m,p,l);}
 public static void Main(string[] args){
  File.AppendAllText(Path.Combine(folder,"lifecycle.txt"),"started:"+string.Join(" ",args)+"\n");
  var c=new WC();c.proc=callback;c.name="LumaShot.UpgradeFixture";c.instance=GetModuleHandleW(null);RegisterClassW(ref c);
  CreateWindowExW(0,c.name,"Synthetic upgrade fixture",0x80000000,0,0,0,0,IntPtr.Zero,IntPtr.Zero,c.instance,IntPtr.Zero);
  MSG msg;while(GetMessageW(out msg,IntPtr.Zero,0,0)>0)DispatchMessageW(ref msg);
 }
}
'@
$exe = Join-Path $payload 'LumaShot.exe'
if (Test-Path -LiteralPath $exe) { Remove-Item -LiteralPath $exe -Force }
Add-Type -TypeDefinition $source -OutputAssembly $exe -OutputType WindowsApplication
$ocrAssets = @('lumashot_ocr_worker.exe','onnxruntime.dll','ocr/det.onnx','ocr/rec.onnx','ocr/dictionary.txt','ocr/LICENSE','ocr/LICENSE-PaddleOCR.txt','ocr/manifest.json','ocr/runtime-build.json','ocr/ThirdPartyNotices.txt')
New-Item -ItemType Directory -Force -Path (Join-Path $payload 'ocr') | Out-Null
foreach ($asset in $ocrAssets) {
 Set-Content -LiteralPath (Join-Path $payload $asset) -Value "Synthetic optional OCR asset: $asset" -Encoding UTF8
}
$script = Get-Content -Raw (Join-Path $root 'scripts/installer.iss')
$script = $script.Replace('AppId={{7C856859-ABF1-40C1-9E85-F5E658390CB3}',"AppId=LumaShotIsolatedUpgradeFixture`r`nUninstallable=no`r`nCreateUninstallRegKey=no`r`nUsePreviousAppDir=no")
$script = $script.Replace('LumaShot.Host', 'LumaShot.UpgradeFixture')
$script = [regex]::Replace($script,'(?s)\[Icons\].*?(?=\[Run\])','')
$iss = Join-Path $testRoot 'fixture.iss'
Set-Content -LiteralPath $iss -Value $script -Encoding UTF8
$iscc = Join-Path $env:LOCALAPPDATA 'Programs/Inno Setup 6/ISCC.exe'
& $iscc /Q "/DPayloadDir=$payload" "/DOutputDirPath=$testRoot" $iss
if ($LASTEXITCODE -ne 0) { throw 'Fixture installer compilation failed' }
$setup = Join-Path $testRoot 'LumaShot-Setup.exe'
$log = Join-Path $install 'lifecycle.txt'
if (Test-Path -LiteralPath $log) { Remove-Item -LiteralPath $log -Force }
$sentinel = Join-Path $install 'synthetic-settings.ini'
Set-Content -LiteralPath $sentinel -Value 'synthetic-setting=retained'
$before = (Get-FileHash -LiteralPath $sentinel).Hash
New-Item -ItemType Directory -Force -Path (Join-Path $install 'ocr') | Out-Null
$unknownAsset = Join-Path $install 'ocr/user-note.txt'
Set-Content -LiteralPath $unknownAsset -Value 'Synthetic user-owned OCR note; preserve on deselection.'
$unknownBefore = (Get-FileHash -LiteralPath $unknownAsset).Hash
$componentPasses = @('core','core,ocr','core','core,ocr')
try {
 for ($pass=1; $pass -le $componentPasses.Count; $pass++) {
  $components = $componentPasses[$pass-1]
  $withOcr = $components -eq 'core,ocr'
  $proc = Start-Process -FilePath $setup -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART',('/COMPONENTS="'+$components+'"'),('/DIR="'+$install+'"'),('/LOG="'+(Join-Path $testRoot "install-$pass.log")+'"')) -WindowStyle Hidden -PassThru
  if (-not $proc.WaitForExit(45000)) { throw 'Fixture installer timeout' }
  if ($proc.ExitCode -ne 0) { throw "Fixture installer failed: $($proc.ExitCode)" }
  $deadline = (Get-Date).AddSeconds(10)
  do { Start-Sleep -Milliseconds 100; $lines = @(Get-Content -LiteralPath $log -ErrorAction SilentlyContinue) } while (@($lines | Where-Object { $_ -eq 'started:--background' }).Count -lt $pass -and (Get-Date) -lt $deadline)
  if (@($lines | Where-Object { $_ -eq 'started:--background' }).Count -ne $pass) { throw 'Background relaunch missing' }
  if (@($lines | Where-Object { $_ -eq 'closed' }).Count -ne ($pass-1)) { throw "Pass ${pass}: old process did not exit through WM_CLOSE" }
  foreach ($asset in $ocrAssets) {
   $installedAsset = Join-Path $install $asset
   if ((Test-Path -LiteralPath $installedAsset) -ne $withOcr) { throw "Pass ${pass}: unexpected OCR asset presence: $asset" }
   if ($withOcr -and (Get-FileHash -LiteralPath $installedAsset).Hash -ne (Get-FileHash -LiteralPath (Join-Path $payload $asset)).Hash) { throw "Pass ${pass}: OCR asset mismatch: $asset" }
  }
  if ((Get-FileHash -LiteralPath $sentinel).Hash -ne $before) { throw "Pass ${pass}: synthetic settings modified" }
  if ((Get-FileHash -LiteralPath $unknownAsset).Hash -ne $unknownBefore) { throw "Pass ${pass}: user-owned OCR note modified" }
  if ((Get-FileHash -LiteralPath (Join-Path $install 'LumaShot.exe')).Hash -ne (Get-FileHash -LiteralPath $exe).Hash) { throw "Pass ${pass}: installed core payload mismatch" }
  Write-Output "PASS: $components installation retains core/settings/user note and restarts in background (pass $pass)"
 }
 Write-Output 'PASS: OCR omitted, installed, removed and restored; each running upgrade closes gracefully and preserves synthetic user files'
} finally {
 # This is an isolated fixture, never a user process.
 Get-Process -Name LumaShot -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq (Join-Path $install 'LumaShot.exe') } | Stop-Process -Force
}
