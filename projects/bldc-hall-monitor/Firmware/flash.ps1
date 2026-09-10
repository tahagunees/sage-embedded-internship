param([string]$Serial = '002C00353235510C37333439', [string]$Programmer = '')
$ErrorActionPreference = 'Stop'
if (-not $Programmer) {
    $Programmer = (Get-ChildItem 'C:/ST' -Recurse -Filter STM32_Programmer_CLI.exe -File | Select-Object -First 1).FullName
}
if (-not $Programmer) { throw 'STM32CubeProgrammer bulunamadi.' }
$binary = Join-Path $PSScriptRoot 'build/hall-monitor.bin'
if (-not (Test-Path -LiteralPath $binary)) { throw 'Once build.ps1 ile derleyin.' }
$connection = @('-c','port=SWD',"sn=$Serial",'mode=UR')
$identify = & $Programmer @connection -r32 0x1FFF75E0 1 2>&1
$identify | Write-Output
if ($LASTEXITCODE -ne 0 -or ($identify -join "`n") -notmatch 'Device name\s*:\s*STM32G491') { throw 'Beklenen STM32G491 karti dogrulanamadi; yazma yapilmadi.' }
$backupDir = Join-Path $PSScriptRoot 'backups'
New-Item -ItemType Directory -Path $backupDir -Force | Out-Null
$backup = Join-Path $backupDir ('before-hall-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '.bin')
& $Programmer @connection -u 0x08000000 0x80000 $backup
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $backup) -or (Get-Item -LiteralPath $backup).Length -ne 524288) { throw 'Tam Flash yedegi alinamadi; yazma yapilmadi.' }
$hash = (Get-FileHash -LiteralPath $backup -Algorithm SHA256).Hash
Set-Content -LiteralPath ($backup + '.sha256') -Value $hash
Write-Host "Mevcut program yedeklendi: $backup"
& $Programmer @connection -d $binary 0x08000000 -v -rst
if ($LASTEXITCODE -ne 0) { throw "Yukleme veya dogrulama basarisiz. Yedek: $backup" }
Write-Host 'Hall monitor karta yuklendi ve dogrulandi.'
