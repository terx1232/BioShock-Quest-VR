# Снимок клиентской области окна BioShock: shot.ps1 <файл.png> [задержка_сек]
param([string]$Out = "D:\BioshockVRMQ2\work\shot.png", [int]$Delay = 0)
if ($Delay -gt 0) { Start-Sleep -Seconds $Delay }
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public class ShotWin {
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
  public struct RECT { public int L, T, R, B; } public struct POINT { public int X, Y; }
}
"@
$p = Get-Process Bioshock -ErrorAction Stop | Select-Object -First 1
$h = $p.MainWindowHandle
[ShotWin]::ShowWindow($h, 9) | Out-Null
[ShotWin]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 700
$c = New-Object ShotWin+RECT; [ShotWin]::GetClientRect($h, [ref]$c) | Out-Null
$o = New-Object ShotWin+POINT; [ShotWin]::ClientToScreen($h, [ref]$o) | Out-Null
$bmp = New-Object System.Drawing.Bitmap $c.R, $c.B
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($o.X, $o.Y, 0, 0, $bmp.Size)
$bmp.Save($Out)
"saved $Out ($($c.R)x$($c.B))"
