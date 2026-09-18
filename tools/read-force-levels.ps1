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
    if ($line -notmatch '^LEVELS,(v14|v15|v16|v17|v18|v19|v20|v21),2,(\d+),(\d+),(\d+),(\d+),(\d+)$') {
        throw "Unexpected level response: $line"
    }
    [ordered]@{
        version=$Matches[1]
        lock=[int]$Matches[2]
        click=[int]$Matches[3]
        release=[int]$Matches[4]
        moving_lock=[int]$Matches[5]
        moving_click=[int]$Matches[6]
    } | ConvertTo-Json
} finally {
    if ($serial.IsOpen) { $serial.Close() }
    $serial.Dispose()
}
