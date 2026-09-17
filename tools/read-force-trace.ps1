param(
    [Parameter(Mandatory=$true)][string]$Port,
    [Parameter(Mandatory=$true)][string]$Output,
    [string]$StartFile
)
$ErrorActionPreference = 'Stop'
# Mouse output remains the left half's BLE connection. This port is only the
# right half's dedicated diagnostics CDC interface. No keyboard data is read.
$serial = [System.IO.Ports.SerialPort]::new($Port, 115200)
$serial.ReadTimeout = 4000
$serial.WriteTimeout = 1000
$serial.NewLine = "`n"
$serial.DtrEnable = $true
function Request-Line([string]$Command) {
    $serial.Write($Command)
    return $serial.ReadLine().Trim()
}
try {
    $serial.Open()
    Start-Sleep -Milliseconds 300
    $serial.DiscardInBuffer()
    $status = Request-Line 'S'
    if ($status -notmatch '^STATUS,v11,1,') {
        throw "Unexpected firmware: $status"
    }
    @{state='ready';port=$Port;status=$status} | ConvertTo-Json -Compress
    if ($StartFile) {
        $deadline = [DateTime]::UtcNow.AddSeconds(180)
        while (-not (Test-Path -LiteralPath $StartFile)) {
            if ([DateTime]::UtcNow -gt $deadline) { throw 'Start marker timeout' }
            Start-Sleep -Milliseconds 25
        }
    }
    $hostBefore = [System.Diagnostics.Stopwatch]::GetTimestamp()
    $arm = Request-Line 'A'
    $hostAfter = [System.Diagnostics.Stopwatch]::GetTimestamp()
    if ($arm -notmatch '^ARM,v11,1,') { throw "Cannot arm; lift fingers first: $arm" }
    @{state='recording';arm=$arm} | ConvertTo-Json -Compress
    Start-Sleep -Milliseconds 10200
    @{state='lift_fingers_for_download'} | ConvertTo-Json -Compress
    $deadline = [DateTime]::UtcNow.AddSeconds(60)
    do {
        $header = Request-Line 'D'
        if ($header -eq 'BUSY') { Start-Sleep -Milliseconds 250 }
        if ([DateTime]::UtcNow -gt $deadline) { throw 'Still touching; download timeout' }
    } while ($header -eq 'BUSY')
    if ($header -notmatch '^DATA,v11,1,(\d+),') { throw "Unexpected header: $header" }
    $count = [int]$Matches[1]
    $lines = [System.Collections.Generic.List[string]]::new()
    $lines.Add($header)
    for ($i = 0; $i -lt $count; $i++) {
        $line = Request-Line 'N'
        if (-not $line.StartsWith("R,$i,")) { throw "Transfer stopped at $i : $line" }
        $lines.Add($line)
    }
    $end = Request-Line 'N'
    if ($end -ne "END,$count") { throw "Missing end marker: $end" }
    $lines.Add($end)
    $result = [ordered]@{
        version='v11';port=$Port;status=$status;arm=$arm;
        host_arm_before_ticks=$hostBefore;host_arm_after_ticks=$hostAfter;
        host_clock_hz=[System.Diagnostics.Stopwatch]::Frequency;
        lines=$lines.ToArray()
    }
    $result | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $Output -Encoding utf8
    @{state='complete';records=$count;output=$Output} | ConvertTo-Json -Compress
} finally {
    if ($serial.IsOpen) { $serial.Close() }
    $serial.Dispose()
}
