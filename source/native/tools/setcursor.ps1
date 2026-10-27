# Ставит курсор Windows в точку клиентской области окна BioShock: setcursor.ps1 x y
param([int]$X, [int]$Y)
Add-Type @"
using System; using System.Runtime.InteropServices;
public class CurWin {
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  public struct POINT { public int X, Y; }
}
"@
$h = (Get-Process Bioshock).MainWindowHandle
[CurWin]::SetForegroundWindow($h) | Out-Null
$p = New-Object CurWin+POINT; $p.X = $X; $p.Y = $Y
[CurWin]::ClientToScreen($h, [ref]$p) | Out-Null
# несколько шагов, чтобы игра увидела движение
for ($i = 0; $i -lt 10; $i++) { [CurWin]::SetCursorPos($p.X - 10 + $i, $p.Y - 10 + $i) | Out-Null; Start-Sleep -Milliseconds 30 }
[CurWin]::SetCursorPos($p.X, $p.Y) | Out-Null
"cursor at client $X,$Y"
