param([string]$Executable = (Join-Path $PSScriptRoot '../build/LumaShot.exe'))
$ErrorActionPreference='Stop'
Add-Type @'
using System;using System.Text;using System.Runtime.InteropServices;
public static class IpcTestWindows {
 [StructLayout(LayoutKind.Sequential)]struct CopyData {public UIntPtr tag;public int size;public IntPtr data;}
 public delegate bool EnumProc(IntPtr h,IntPtr p);
 [DllImport("user32.dll")]public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
 [DllImport("user32.dll")]static extern bool EnumWindows(EnumProc p,IntPtr n);
 [DllImport("user32.dll")]static extern uint GetWindowThreadProcessId(IntPtr h,out uint id);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern int GetClassName(IntPtr h,StringBuilder s,int n);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern uint RegisterWindowMessage(string name);
 [DllImport("user32.dll")]public static extern IntPtr SendMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll",SetLastError=true)]static extern IntPtr SendMessageTimeout(IntPtr h,uint m,IntPtr w,ref CopyData l,uint flags,uint timeout,out UIntPtr result);
 [DllImport("user32.dll")]public static extern uint GetClipboardSequenceNumber();
 public static IntPtr Find(int id,string name){IntPtr result=IntPtr.Zero;EnumWindows((h,p)=>{uint pid;GetWindowThreadProcessId(h,out pid);if(id==0||pid==(uint)id){var s=new StringBuilder(128);GetClassName(h,s,128);if(s.ToString()==name){result=h;return false;}}return true;},IntPtr.Zero);return result;}
 public static ulong Send(IntPtr h,uint tag,string text){var data=Marshal.StringToHGlobalUni(text);try{var c=new CopyData{tag=new UIntPtr(tag),size=(text.Length+1)*2,data=data};UIntPtr result;if(SendMessageTimeout(h,0x4a,IntPtr.Zero,ref c,2,5000,out result)==IntPtr.Zero)throw new Exception("WM_COPYDATA timed out or failed: "+Marshal.GetLastWin32Error());return result.ToUInt64();}finally{Marshal.FreeHGlobal(data);}}
 public static void Mouse(IntPtr h,uint message,int x,int y){SendMessage(h,message,new IntPtr(message==0x201||message==0x200?1:0),new IntPtr((y<<16)|x));}
 public static void Key(IntPtr h,int key){SendMessage(h,0x100,new IntPtr(key),IntPtr.Zero);}
}
'@
Add-Type -AssemblyName System.Drawing
[IpcTestWindows]::SetThreadDpiAwarenessContext([IntPtr](-4)) | Out-Null
$Executable=(Resolve-Path -LiteralPath $Executable).Path
$work=Join-Path (Join-Path $PSScriptRoot '../build') ('capture-ipc-test-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
$initialClients=@(Get-Process -Name '*CaptureClient*' -ErrorAction SilentlyContinue | ForEach-Object Id)
$sequence=[IpcTestWindows]::GetClipboardSequenceNumber()
$process=Start-Process -FilePath $Executable -ArgumentList '--capture-ipc-demo' -WindowStyle Hidden -PassThru
$hostPid=$process.Id
function Assert-Host {
 $process.Refresh()
 if($process.HasExited-or$process.Id-ne$hostPid){throw 'Resident host exited or PID changed'}
 if([IpcTestWindows]::Find($hostPid,'LumaShot.CaptureClient')-ne[IntPtr]::Zero){throw 'Unexpected legacy capture client window'}
 $newClients=@(Get-Process -Name '*CaptureClient*' -ErrorAction SilentlyContinue | Where-Object {$_.Id -notin $initialClients})
 if($newClients.Count){throw 'Unexpected CaptureClient process'}
 if([IpcTestWindows]::GetClipboardSequenceNumber()-ne$sequence){throw 'Clipboard sequence changed'}
}
function Wait-Window([string]$Class) {
 $deadline=[datetime]::UtcNow.AddSeconds(20)
 do {Assert-Host;$window=[IpcTestWindows]::Find($hostPid,$Class);if($window-ne[IntPtr]::Zero){return $window};Start-Sleep -Milliseconds 15}while([datetime]::UtcNow-lt$deadline)
 throw "Window did not appear: $Class"
}
function New-Request([string]$Name) {
 $nonce=[guid]::NewGuid().ToString('N')
 $events=@('ok','cancel','error' | ForEach-Object {[Threading.EventWaitHandle]::new($false,[Threading.EventResetMode]::ManualReset,"Local\LumaShot.Capture.$nonce.$_")})
 $file=Join-Path $work "$Name.png"
 return @{Nonce=$nonce;Events=$events;File=$file;Payload="1`n$nonce`n$PID`n$file"}
}
function Close-Request($Request) {foreach($event in $Request.Events){$event.Dispose()}}
function Finish-Request($Request,[int]$Expected) {
 $status=[Threading.WaitHandle]::WaitAny([Threading.WaitHandle[]]$Request.Events,20000)
 if($status-ne$Expected){throw "Wrong terminal event: expected $Expected, received $status"}
 for($i=0;$i-lt3;$i++){if($i-ne$Expected-and$Request.Events[$i].WaitOne(0)){throw 'Multiple terminal events signaled'}}
 if($Expected-eq0){
  $image=[Drawing.Image]::FromFile($Request.File)
  try {if($image.Width-ne300-or$image.Height-ne200){throw "Unexpected dimensions $($image.Width)x$($image.Height)"}}finally{$image.Dispose()}
 }elseif(Test-Path -LiteralPath $Request.File){throw 'Cancelled request created output'}
 Assert-Host
}
try {
 $hostWindow=Wait-Window 'LumaShot.CaptureIPC.Test'
 $process.Refresh();$initialHandles=$process.HandleCount;$initialBytes=$process.PrivateMemorySize64
 if([IpcTestWindows]::SendMessage($hostWindow,[IpcTestWindows]::RegisterWindowMessage('LumaShot.CaptureIPC.Version'),[IntPtr]::Zero,[IntPtr]::Zero).ToInt64()-ne1){throw 'Unsupported IPC version'}
 if([IpcTestWindows]::Find($hostPid,'LumaShot.Overlay')-ne[IntPtr]::Zero){throw 'Idle IPC demo unexpectedly opened overlay'}
 $modes=@('complete','cancel','complete-again','busy','cancel-ipc')+@(1..10 | ForEach-Object {if($_%2-eq0){"warm-cancel-$_"}else{"warm-complete-$_"}})
 $warmSamples=@()
 foreach($mode in $modes) {
  $request=New-Request $mode
  try {
   $timer=[Diagnostics.Stopwatch]::StartNew()
   if([IpcTestWindows]::Send($hostWindow,0x4c534331,$request.Payload)-ne1){throw 'Valid request rejected'}
   $overlay=Wait-Window 'LumaShot.Overlay';$overlayMs=$timer.Elapsed.TotalMilliseconds
   if($mode-eq'busy') {
    $other=New-Request 'busy-rejected'
    try {if([IpcTestWindows]::Send($hostWindow,0x4c534331,$other.Payload)-ne2){throw 'Busy request not rejected'};if([Threading.WaitHandle]::WaitAny([Threading.WaitHandle[]]$other.Events,0)-ne[Threading.WaitHandle]::WaitTimeout){throw 'Rejected request signaled event'};if(Test-Path -LiteralPath $other.File){throw 'Rejected request wrote file'}}finally{Close-Request $other}
   }
   if($mode-eq'cancel-ipc'){if([IpcTestWindows]::Send($hostWindow,0x4c534332,$request.Nonce)-ne1){throw 'IPC cancellation rejected'};Finish-Request $request 1}
   elseif($mode-eq'cancel'-or$mode-like'warm-cancel-*'){[IpcTestWindows]::Key($overlay,27);Finish-Request $request 1}
   else {[IpcTestWindows]::Mouse($overlay,0x201,150,150);[IpcTestWindows]::Mouse($overlay,0x200,450,350);[IpcTestWindows]::Mouse($overlay,0x202,450,350);[IpcTestWindows]::Key($overlay,13);Finish-Request $request 0}
   $process.Refresh()
   if($mode-eq'complete'){$warmBaselineHandles=$process.HandleCount;$warmBaselineBytes=$process.PrivateMemorySize64}
   if($mode-like'warm-*'){$warmSamples+=@{Handles=$process.HandleCount;Bytes=$process.PrivateMemorySize64}}
   Write-Output ('PASS {0}: PID={1}; synthetic overlay={2:F1} ms; terminal event={3:F1} ms; handles={4}; private bytes={5}' -f $mode,$hostPid,$overlayMs,$timer.Elapsed.TotalMilliseconds,$process.HandleCount,$process.PrivateMemorySize64)
  }finally{Close-Request $request}
 }
 if($warmSamples[-1].Handles-$warmSamples[0].Handles-gt10){throw 'Warm loop handle count kept growing'}
 if($warmSamples[-1].Bytes-$warmSamples[0].Bytes-gt16MB){throw 'Warm loop private memory grew by more than 16 MiB'}
 Write-Output ('Warm resource sample: first-completion handles={0}, private bytes={1}; loop start={2}/{3}; loop end={4}/{5}' -f $warmBaselineHandles,$warmBaselineBytes,$warmSamples[0].Handles,$warmSamples[0].Bytes,$warmSamples[-1].Handles,$warmSamples[-1].Bytes)
 # The child supplies the monitored caller PID; the parent sends and holds events.
 $caller=Start-Process -FilePath (Get-Process -Id $PID).Path -ArgumentList @('-NoProfile','-Command','Start-Sleep -Seconds 60') -WindowStyle Hidden -PassThru
 $orphan=New-Request 'caller-death'
 try {
  $orphan.Payload="1`n$($orphan.Nonce)`n$($caller.Id)`n$($orphan.File)"
  if([IpcTestWindows]::Send($hostWindow,0x4c534331,$orphan.Payload)-ne1){throw 'Simulated caller request rejected'}
  $null=Wait-Window 'LumaShot.Overlay'
  $caller.Kill();$caller.WaitForExit()
  Finish-Request $orphan 1
  $deadline=[datetime]::UtcNow.AddSeconds(5)
  while([IpcTestWindows]::Find($hostPid,'LumaShot.Overlay')-ne[IntPtr]::Zero-and[datetime]::UtcNow-lt$deadline){Start-Sleep -Milliseconds 15}
  if([IpcTestWindows]::Find($hostPid,'LumaShot.Overlay')-ne[IntPtr]::Zero){throw 'Caller death left overlay open'}
  Write-Output 'PASS simulated caller process death: cancel event, no output, overlay closed'
 }finally{Close-Request $orphan;if(-not$caller.HasExited){$caller.Kill();$caller.WaitForExit()};$caller.Dispose()}
 $recovery=New-Request 'after-caller-death'
 try {
  if([IpcTestWindows]::Send($hostWindow,0x4c534331,$recovery.Payload)-ne1){throw 'Host did not recover after caller death'}
  $overlay=Wait-Window 'LumaShot.Overlay'
  [IpcTestWindows]::Mouse($overlay,0x201,150,150);[IpcTestWindows]::Mouse($overlay,0x200,450,350);[IpcTestWindows]::Mouse($overlay,0x202,450,350);[IpcTestWindows]::Key($overlay,13)
  Finish-Request $recovery 0
  Write-Output 'PASS normal 300x200 capture after caller death, same resident PID'
 }finally{Close-Request $recovery}
 $invalid=New-Request 'existing'
 try {
  [IO.File]::WriteAllText($invalid.File,'SYNTHETIC-FIXTURE-DO-NOT-REPLACE')
  foreach($payload in @('malformed',$invalid.Payload,$invalid.Payload.Replace("1`n","2`n"))) {if([IpcTestWindows]::Send($hostWindow,0x4c534331,$payload)-ne0){throw 'Invalid request accepted'}}
  if([IO.File]::ReadAllText($invalid.File)-ne'SYNTHETIC-FIXTURE-DO-NOT-REPLACE'){throw 'Existing file modified'}
  if([Threading.WaitHandle]::WaitAny([Threading.WaitHandle[]]$invalid.Events,0)-ne[Threading.WaitHandle]::WaitTimeout){throw 'Invalid request signaled event'}
  Assert-Host
  Write-Output 'PASS malformed, protocol version, existing-file rejection; clipboard sequence unchanged'
 }finally{Close-Request $invalid}
 $process.Refresh()
 Write-Output ("Host resource sample: handles {0} -> {1}; private bytes {2} -> {3}" -f $initialHandles,$process.HandleCount,$initialBytes,$process.PrivateMemorySize64)
 Write-Output "All resident IPC integration tests passed. Synthetic timings include test interaction, not desktop capture performance. Artifacts: $work"
}finally{if(-not$process.HasExited){$process.Kill();$process.WaitForExit()};$process.Dispose()}
