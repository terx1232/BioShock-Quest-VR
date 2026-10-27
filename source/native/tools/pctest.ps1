# PC-тест мода: перезапуск игры с модом и симулятором шлема, команды отладки, снимок.
#   pctest.ps1 -Extra "TheaterDraw=0" -Commands "point 0.5 0.74","wait 30","press R_TRIGGER 8" -WaitMenu 70 -After 6
param([string[]]$Extra = @(), [string[]]$Commands = @(), [int]$WaitMenu = 70, [int]$After = 6,
      [string]$Shot = "D:\BioshockVRMQ2\work\pctest.png", [switch]$NoRestart)
$dir = "D:\BioshockVRMQ2\output\BioshockMQ2\Builds\Release"
if (-not $NoRestart) {
    Stop-Process -Name Bioshock -Force -ErrorAction SilentlyContinue
    Get-CimInstance Win32_Process -Filter "Name='py.exe' or Name='python.exe'" | Where-Object { $_.CommandLine -like "*xrsim*" } |
        ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
    Wait-Process -Name Bioshock -Timeout 20 -ErrorAction SilentlyContinue
    for ($t = 0; $t -lt 20; $t++) {
        try { Copy-Item D:\BioshockVRMQ2\mod\out\d3d9.dll $dir -Force -ErrorAction Stop; break } catch { Start-Sleep 1 }
    }
    $ini = (Get-Content D:\BioshockVRMQ2\mod\BioshockVR.ini) -replace '^Stereo=.*', 'Stereo=0'
    # дополнительные ключи заменяют одноимённые в ini (GetPrivateProfileString берёт первый)
    foreach ($e in $Extra) {
        $k = ($e -split '=', 2)[0]
        $ini = $ini | Where-Object { $_ -notmatch ('^' + [regex]::Escape($k) + '=') }
    }
    $ini += $Extra
    $ini | Set-Content "$dir\BioshockVR.ini" -Encoding utf8
    Start-Process -FilePath "$dir\Bioshock.exe" -ArgumentList "-dx9", "-windowed" -WorkingDirectory $dir | Out-Null
    Start-Process -FilePath "py" -ArgumentList "-3", "-I", "D:\BioshockVRMQ2\mod\tools\xrsim.py", "--seconds", "900", "--yaw", "0", "--pitch", "0" -WindowStyle Hidden | Out-Null
    # Окно игры в фокус: без фокуса игра не опрашивает мышь (калибровка курсора не пройдёт)
    Add-Type @"
using System; using System.Runtime.InteropServices;
public class FocusWin { [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h); }
"@
    for ($i = 0; $i -lt $WaitMenu; $i += 5) {
        Start-Sleep -Seconds 5
        $g = Get-Process Bioshock -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($g -and $g.MainWindowHandle -ne 0) { [FocusWin]::SetForegroundWindow($g.MainWindowHandle) | Out-Null }
    }
}
if ($Commands.Count -gt 0) {
    $Commands | Set-Content "$dir\vrcmd.txt" -Encoding ascii
    Start-Sleep -Seconds $After
}
& D:\BioshockVRMQ2\mod\tools\shot.ps1 -Out $Shot
Get-Content "$dir\BioshockVR.log" | Select-String -Pattern "Dev:|Mouse: click|UI:|Mode:|EXCEPTION" | Select-Object -Last 8
