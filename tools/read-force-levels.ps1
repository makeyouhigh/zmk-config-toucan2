param([Parameter(Mandatory=$true)][string]$Port)
$ErrorActionPreference = 'Stop'
$serial = [System.IO.Ports.SerialPort]::new($Port, 115200)
$serial.ReadTimeout = 4000
$serial.WriteTimeout = 1000
$serial.NewLine = "`n"
$serial.DtrEnable = $true
try {
    $serial.Open()
    Start-Sleep -Milliseconds 300
    $serial.DiscardInBuffer()
    $serial.Write('G')
    $line = $serial.ReadLine().Trim()
    if ($line -notmatch '^LEVELS,v14,2,(\d+),(\d+),(\d+),(\d+),(\d+)$') {
        throw "Unexpected level response: $line"
    }
    [ordered]@{
        version='v14'
        lock=[int]$Matches[1]
        click=[int]$Matches[2]
        release=[int]$Matches[3]
        moving_lock=[int]$Matches[4]
        moving_click=[int]$Matches[5]
    } | ConvertTo-Json
} finally {
    if ($serial.IsOpen) { $serial.Close() }
    $serial.Dispose()
}
