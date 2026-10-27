# Отладка мода на шлеме по adb: команды мода (vrcmd.txt), снимок кадра, лог.
#   quest.ps1 -Commands "point 0.5 0.74","press R_TRIGGER 20"   - отправить команды
#   quest.ps1 -Shot D:\BioshockVRMQ2\work\q.png -Wait 3          - снимок кадра, уходящего в шлем
#   quest.ps1 -Log 20                                            - последние строки лога мода
param([string[]]$Commands = @(), [string]$Shot = "", [int]$Wait = 2, [int]$Log = 0, [string]$Grep = "")
$adb = "D:\Android\Sdk\platform-tools\adb.exe"
$game = "files/game/Builds/Release"
$pkg = "com.winlator.cmod"
function Send([string[]]$lines) {
    $tmp = Join-Path $env:TEMP "vrcmd.txt"
    ($lines -join "`n") + "`n" | Set-Content $tmp -Encoding ascii -NoNewline
    & $adb push $tmp /data/local/tmp/vrcmd.txt | Out-Null
    & $adb shell "chmod 644 /data/local/tmp/vrcmd.txt; run-as $pkg cp /data/local/tmp/vrcmd.txt $game/vrcmd.txt"
}
if ($Commands.Count -gt 0) { Send $Commands; "sent: $($Commands -join ' | ')" }
if ($Shot -ne "") {
    & $adb shell "run-as $pkg rm -f $game/vrshot.bmp"
    Send @("shot")
    Start-Sleep -Seconds $Wait
    $bmp = [System.IO.Path]::ChangeExtension($Shot, ".bmp")
    Start-Process -FilePath $adb -ArgumentList "exec-out","run-as",$pkg,"cat","$game/vrshot.bmp" -RedirectStandardOutput $bmp -NoNewWindow -Wait
    if ((Test-Path $bmp) -and (Get-Item $bmp).Length -gt 1000) {
        Add-Type -AssemblyName System.Drawing
        $img = [System.Drawing.Image]::FromFile($bmp); $img.Save($Shot, [System.Drawing.Imaging.ImageFormat]::Png); $img.Dispose()
        "shot: $Shot"
    } else { "no shot (game not presenting?)" }
}
if ($Log -gt 0) {
    $lines = & $adb exec-out run-as $pkg cat "$game/BioshockVR.log"
    if ($Grep -ne "") { $lines = $lines | Select-String -Pattern $Grep }
    $lines | Select-Object -Last $Log
}
