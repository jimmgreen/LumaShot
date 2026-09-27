param([string]$Executable = (Join-Path $PSScriptRoot '../build/LumaShot.exe'))
$ErrorActionPreference='Stop'
Add-Type @'
using System;using System.Collections.Generic;using System.Text;using System.Runtime.InteropServices;
public static class CaptureTestWindows {
 [DllImport("user32.dll")]public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
 public delegate bool EnumProc(IntPtr h,IntPtr p);
 [DllImport("user32.dll")]public static extern bool EnumWindows(EnumProc p,IntPtr n);
 [DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint id);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern int GetClassName(IntPtr h,StringBuilder s,int n);
 [DllImport("user32.dll")]public static extern IntPtr SendMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")]public static extern uint GetClipboardSequenceNumber();
 public static IntPtr Find(int id,string name){IntPtr result=IntPtr.Zero;EnumWindows((h,p)=>{uint pid;GetWindowThreadProcessId(h,out pid);if(pid==(uint)id){var s=new StringBuilder(128);GetClassName(h,s,128);if(s.ToString()==name){result=h;return false;}}return true;},IntPtr.Zero);return result;}
 public static void Mouse(IntPtr h,uint message,int x,int y){SendMessage(h,message,new IntPtr(message==0x201||message==0x200?1:0),new IntPtr((y<<16)|x));}
 public static void Key(IntPtr h,int key){SendMessage(h,0x100,new IntPtr(key),IntPtr.Zero);}
}
'@
[CaptureTestWindows]::SetThreadDpiAwarenessContext([IntPtr](-4)) | Out-Null
$Executable=(Resolve-Path -LiteralPath $Executable).Path
$work=Join-Path (Join-Path $PSScriptRoot '../build') ('capture-client-test-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
function Run-Case([string]$Name,[bool]$Cancel) {
 $file=Join-Path $work "$Name.png"
 $sequence=[CaptureTestWindows]::GetClipboardSequenceNumber()
 $process=Start-Process -FilePath $Executable -ArgumentList @('--capture-result-demo',('"'+$file+'"')) -PassThru
 try {
  $deadline=[datetime]::UtcNow.AddSeconds(20);$overlay=[IntPtr]::Zero
  do {if($process.HasExited){throw 'Process exited before synthetic view opened'};Start-Sleep -Milliseconds 80;$overlay=[CaptureTestWindows]::Find($process.Id,'LumaShot.Overlay')}while($overlay-eq[IntPtr]::Zero-and[datetime]::UtcNow-lt$deadline)
  if($overlay-eq[IntPtr]::Zero){throw 'Synthetic view did not open'}
  if([CaptureTestWindows]::Find($process.Id,'LumaShot.CaptureClient')-eq[IntPtr]::Zero){throw 'Wrong host class'}
  if($Cancel){[CaptureTestWindows]::Key($overlay,27)}else{
   [CaptureTestWindows]::Mouse($overlay,0x201,150,150)
   [CaptureTestWindows]::Mouse($overlay,0x200,450,350)
   [CaptureTestWindows]::Mouse($overlay,0x202,450,350)
   [CaptureTestWindows]::Key($overlay,13)
  }
  if(-not$process.WaitForExit(20000)){throw 'Capture process did not exit'}
  $process.Refresh()
  if($Cancel){if($process.ExitCode-ne2-or(Test-Path -LiteralPath $file)){throw "Cancellation failed: $($process.ExitCode)"}}
  else {
   if($process.ExitCode-ne0-or-not(Test-Path -LiteralPath $file)){throw "Completion failed: $($process.ExitCode)"}
   Add-Type -AssemblyName System.Drawing
   $image=[System.Drawing.Image]::FromFile($file)
   try {if($image.Width-ne300-or$image.Height-ne200){throw "Unexpected PNG dimensions $($image.Width)x$($image.Height)"}} finally {$image.Dispose()}
  }
  if([CaptureTestWindows]::GetClipboardSequenceNumber()-ne$sequence){throw 'Clipboard sequence changed during isolated capture'}
  Write-Output "PASS $Name : exit=$($process.ExitCode); synthetic pixels only; clipboard untouched"
 } finally {if(-not$process.HasExited){$process.Kill();$process.WaitForExit()};$process.Dispose()}
}
Run-Case 'complete' $false
Run-Case 'cancel' $true
Run-Case 'complete-again' $false
Write-Output "All capture result protocol tests passed. Artifacts: $work"

$existing=Join-Path $work 'existing.png'
[System.IO.File]::WriteAllText($existing,'SYNTHETIC-FIXTURE-DO-NOT-REPLACE')
foreach($arguments in @(@('--capture-result-demo',('"'+$existing+'"')),@('--capture-result-demo'))) {
 $p=Start-Process -FilePath $Executable -ArgumentList $arguments -PassThru
 try {if(-not$p.WaitForExit(5000)){$p.Kill();throw 'Invalid invocation did not fail promptly'};$p.Refresh();if($p.ExitCode-ne1){throw 'Invalid invocation not rejected'}}finally{$p.Dispose()}
}
if([System.IO.File]::ReadAllText($existing)-ne'SYNTHETIC-FIXTURE-DO-NOT-REPLACE'){throw 'Existing output was modified'}
Write-Output 'PASS existing output and malformed arguments rejected without launching a capture'
