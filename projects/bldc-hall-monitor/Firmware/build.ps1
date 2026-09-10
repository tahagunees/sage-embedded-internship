param([string]$CubeRoot = '', [string]$ToolBin = '')
$ErrorActionPreference = 'Stop'
if (-not $CubeRoot) {
    $CubeRoot = (Get-ChildItem "$env:USERPROFILE/STM32Cube/Repository" -Directory -Filter 'STM32Cube_FW_G4*' | Sort-Object Name -Descending | Select-Object -First 1).FullName
}
if (-not $ToolBin) {
    $gcc = Get-ChildItem 'C:/ST' -Recurse -Filter arm-none-eabi-gcc.exe -File | Select-Object -First 1
    if ($gcc) { $ToolBin = $gcc.DirectoryName }
}
if (-not $CubeRoot -or -not $ToolBin) { throw 'STM32Cube G4 paketi veya ARM derleyicisi bulunamadi. CubeRoot ve ToolBin belirtin.' }
$cmsis = Join-Path $CubeRoot 'Drivers/CMSIS'
$device = Join-Path $cmsis 'Device/ST/STM32G4xx'
$output = Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$elf = Join-Path $output 'hall-monitor.elf'
$argsList = @('-mcpu=cortex-m4','-mthumb','-mfloat-abi=soft','-DSTM32G491xx','-Os','-g3','-std=c11','-Wall','-Wextra','-Werror','-ffunction-sections','-fdata-sections','-nostartfiles','--specs=nano.specs','--specs=nosys.specs',"-I$cmsis/Include","-I$device/Include","$PSScriptRoot/main.c","$device/Source/Templates/system_stm32g4xx.c","$device/Source/Templates/gcc/startup_stm32g491xx.s","-T$PSScriptRoot/STM32G491RE.ld",'-Wl,--gc-sections',"-Wl,-Map=$output/hall-monitor.map",'-Wl,--no-warn-rwx-segments','-o',$elf)
& "$ToolBin/arm-none-eabi-gcc.exe" @argsList
if ($LASTEXITCODE -ne 0) { throw 'Firmware derlenemedi.' }
& "$ToolBin/arm-none-eabi-objcopy.exe" -O binary $elf "$output/hall-monitor.bin"
if ($LASTEXITCODE -ne 0) { throw 'BIN olusturulamadi.' }
& "$ToolBin/arm-none-eabi-size.exe" $elf
Write-Host "Hazir: $output/hall-monitor.bin"
