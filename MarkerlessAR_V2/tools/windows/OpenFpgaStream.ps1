param(
    [Parameter(Mandatory = $true)]
    [string]$FpgaIp,

    [int]$Port = 5969,

    [ValidateSet("auto", "edge", "chrome", "vlc", "browser")]
    [string]$Player = "auto"
)

$streamUrl = "http://$FpgaIp`:$Port/stream.mjpg"

function Test-CommandPath {
    param([string]$Name)

    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($null -eq $command) {
        return $null
    }

    return $command.Source
}

function Find-VlcPath {
    $candidates = @(
        "$env:ProgramFiles\VideoLAN\VLC\vlc.exe",
        "${env:ProgramFiles(x86)}\VideoLAN\VLC\vlc.exe"
    )

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    return $null
}

function Open-InEdge {
    $edgePath = Test-CommandPath "msedge.exe"
    if ($null -eq $edgePath) {
        return $false
    }

    Start-Process -FilePath $edgePath -ArgumentList @("--app=$streamUrl", "--start-fullscreen")
    return $true
}

function Open-InChrome {
    $chromePath = Test-CommandPath "chrome.exe"
    if ($null -eq $chromePath) {
        $candidates = @(
            "$env:ProgramFiles\Google\Chrome\Application\chrome.exe",
            "${env:ProgramFiles(x86)}\Google\Chrome\Application\chrome.exe"
        )

        foreach ($candidate in $candidates) {
            if (Test-Path $candidate) {
                $chromePath = $candidate
                break
            }
        }
    }

    if ($null -eq $chromePath) {
        return $false
    }

    Start-Process -FilePath $chromePath -ArgumentList @("--app=$streamUrl", "--start-fullscreen")
    return $true
}

function Open-InVlc {
    $vlcPath = Find-VlcPath
    if ($null -eq $vlcPath) {
        return $false
    }

    Start-Process -FilePath $vlcPath -ArgumentList @("--fullscreen", $streamUrl)
    return $true
}

function Open-InDefaultBrowser {
    Start-Process $streamUrl
    return $true
}

$opened = $false

switch ($Player) {
    "edge"   { $opened = Open-InEdge }
    "chrome" { $opened = Open-InChrome }
    "vlc"    { $opened = Open-InVlc }
    "browser" { $opened = Open-InDefaultBrowser }
    "auto" {
        $opened = (Open-InEdge) -or (Open-InChrome) -or (Open-InVlc) -or (Open-InDefaultBrowser)
    }
}

if (-not $opened) {
    Write-Error "Could not find a supported player. Install Edge, Chrome, or VLC, or use -Player browser."
    exit 1
}

Write-Host "Opened FPGA stream: $streamUrl"
