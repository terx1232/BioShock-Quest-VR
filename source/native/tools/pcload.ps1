# PC-тест: запуск игры с модом и загрузка последнего сохранения (пункт Continue главного меню).
#   pcload.ps1 [-Extra "WeaponPitch=-30"] [-Shot D:\BioshockVRMQ2\work\load.png]
param([string[]]$Extra = @(), [string]$Shot = "D:\BioshockVRMQ2\work\load.png")
$dir = "D:\BioshockVRMQ2\output\BioshockMQ2\Builds\Release"
$log = "$dir\BioshockVR.log"
# запуск игры (меню без кинотеатра: пункт меню виден в кадре как есть)
& D:\BioshockVRMQ2\mod\tools\pctest.ps1 -Extra (@("TheaterDraw=0") + $Extra) -WaitMenu 5 | Out-Null
# главное меню готово, когда мод нашёл курсор игры
for ($i = 0; $i -lt 60; $i++) {
    Start-Sleep 3
    if (Get-Content $log -ErrorAction SilentlyContinue | Select-String "menu cursor range") { break }
}
Start-Sleep 3
# Continue в кадре 1280x1280: (0.5, 0.567); клик мышью и Enter
& D:\BioshockVRMQ2\mod\tools\pctest.ps1 -NoRestart -Commands "point 0.5 0.567", "wait 200", "press R_TRIGGER 20", "wait 30", "key 13", "wait 30", "point off" -After 4 -Shot "$env:TEMP\pcload_menu.png" | Out-Null
for ($i = 0; $i -lt 60; $i++) {
    Start-Sleep 4
    $m = Get-Content $log | Select-String "Mode:" | Select-Object -Last 1
    if ($m -match "game \(VR") { break }
}
Start-Sleep 6
& D:\BioshockVRMQ2\mod\tools\pctest.ps1 -NoRestart -Commands "shot" -After 3 -Shot "$env:TEMP\pcload_shot.png" | Out-Null
Copy-Item "$dir\vrshot.bmp" ([System.IO.Path]::ChangeExtension($Shot, ".bmp")) -Force
Get-Content $log | Select-String "Mode:|Weapon|Dev: key|click|UI:" | Select-Object -Last 8
