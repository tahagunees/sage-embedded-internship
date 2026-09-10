$ErrorActionPreference = 'Stop'
& dotnet build "$PSScriptRoot/Desktop/BldcHallMonitor.csproj" -c Release --nologo
if ($LASTEXITCODE -ne 0) { throw 'Arayuz derlenemedi.' }
& "$PSScriptRoot/Firmware/build.ps1"
