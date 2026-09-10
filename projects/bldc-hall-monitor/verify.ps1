param([string]$PortName = 'COM3', [switch]$OfflineOnly)
$ErrorActionPreference = 'Stop'
if (-not ('BldcHallMonitor.HallFrame' -as [type])) {
    Add-Type -Path (Join-Path $PSScriptRoot 'Desktop/HallModel.cs')
}
function Assert([bool]$Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
$parsed = $null
foreach ($line in @('F1,1,100,SIM,1,1,0,0,0,0','F1,2,110,SIM,5,2,1,1,0,0','F1,3,120,HALL,0,0,0,0,1,0')) {
    Assert ([BldcHallMonitor.HallFrame]::TryParse($line, [ref]$parsed)) "Gecerli satir reddedildi: $line"
}
foreach ($line in @('', 'noise','F1,1,100,SIM,1,6,0,0,0,0','F1,1,100,SIM,8,0,0,0,0,0','F1,1,100,SIM,1,1,-2147483648,0,0,0','F1,1,100,HALL,0,0,1,0,0,0','F1,1,100,OTHER,1,1,0,0,0,0')) {
    Assert (-not [BldcHallMonitor.HallFrame]::TryParse($line, [ref]$parsed)) "Bozuk satir kabul edildi: $line"
}
Write-Output 'PASS: Protocol valid/invalid frames'
if ($OfflineOnly) { return }
$serial = [System.IO.Ports.SerialPort]::new($PortName,115200,[System.IO.Ports.Parity]::None,8,[System.IO.Ports.StopBits]::One)
$serial.ReadTimeout = 150
$serial.WriteTimeout = 200
$serial.NewLine = "`n"
function Exchange([string]$Command, [scriptblock]$Expected) {
    $serial.DiscardInBuffer()
    $serial.Write($Command + "`n")
    $deadline = [DateTime]::UtcNow.AddSeconds(2)
    do {
        try { $line = $serial.ReadLine() } catch [System.TimeoutException] { continue }
        $result = $null
        if ([BldcHallMonitor.HallFrame]::TryParse($line, [ref]$result) -and (& $Expected $result)) { return $result }
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Kart yaniti beklenen duruma ulasmadi: $Command"
}
try {
    $serial.Open()
    $f = Exchange 'M0' { param($f) $f.Mode -eq 'SIM' -and $f.Step -eq 1 }
    $f = Exchange 'R' { param($f) $f.RelativeSteps -eq 0 -and $f.Faults -eq 0 }
    for ($i = 1; $i -le 6; $i++) {
        $expectedStep = ($i % 6) + 1
        $f = Exchange 'N' { param($f) $f.Step -eq $expectedStep -and $f.Direction -eq 1 -and $f.RelativeSteps -eq $i }
    }
    Assert ($f.Step -eq 1 -and $f.RelativeSteps -eq 6 -and $f.Faults -eq 0) 'Ileri tur basarisiz'
    Write-Output 'PASS: Six forward sectors and wraparound on Nucleo'
    for ($i = 1; $i -le 6; $i++) {
        $expectedStep = (6 - $i) % 6 + 1
        $expectedCount = 6 - $i
        $f = Exchange 'P' { param($f) $f.Step -eq $expectedStep -and $f.Direction -eq -1 -and $f.RelativeSteps -eq $expectedCount }
    }
    Assert ($f.Step -eq 1 -and $f.RelativeSteps -eq 0 -and $f.Faults -eq 0) 'Geri tur basarisiz'
    Write-Output 'PASS: Six reverse sectors and relative count returns to zero'
    $f = Exchange 'S4' { param($f) $f.Step -eq 4 -and $f.Faults -eq 1 -and $f.Direction -eq 0 }
    Write-Output 'PASS: Nonadjacent jump detected without invented direction'
    $f = Exchange 'M1' { param($f) $f.Mode -eq 'HALL' }
    $hall = $f.Hall
    $f = Exchange 'N' { param($f) $f.Mode -eq 'HALL' -and $f.Hall -eq $hall }
    Write-Output "PASS: Hardware-input mode, current raw Hall=$hall, simulation commands disabled"
    $f = Exchange 'M0' { param($f) $f.Mode -eq 'SIM' -and $f.Step -eq 1 }
    $f = Exchange 'R' { param($f) $f.Faults -eq 0 -and $f.RelativeSteps -eq 0 }
    Write-Output 'PASS: Restored to SIM / sector 1 / counters 0'
} finally { if ($serial.IsOpen) { $serial.Close() }; $serial.Dispose() }
