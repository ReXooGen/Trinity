$game = Get-Process -Name CrimsonDesert -ErrorAction SilentlyContinue
if ($game) { Write-Host "GAME MASIH JALAN - tutup dulu!"; exit }

$b = "C:\GABUT\apalah\Trinity-update\build-clean\Trinity.asi"
$bytes = [System.IO.File]::ReadAllBytes($b)
$text = [System.Text.Encoding]::ASCII.GetString($bytes)
$isMaster = $text.Contains("1.2.5 ")
$isOld    = $text.Contains("1.2.6-tu2")
Write-Host "build-clean: master(1.2.5)=$isMaster old(1.2.6-tu2)=$isOld"

Copy-Item $b "C:\Program Files (x86)\Steam\steamapps\common\Crimson Desert\bin64\Trinity.asi" -Force
Copy-Item $b "C:\Program Files (x86)\Steam\steamapps\common\Crimson Desert\mods\Trinity.asi" -Force

$d  = [System.IO.File]::ReadAllBytes("C:\Program Files (x86)\Steam\steamapps\common\Crimson Desert\bin64\Trinity.asi")
$dt = [System.Text.Encoding]::ASCII.GetString($d)
Write-Host "bin64 deployed: master=$($dt.Contains('1.2.5 ')) old=$($dt.Contains('1.2.6-tu2'))"
