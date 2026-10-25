@echo off
set "M_CARBINE_SELF=%~f0"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$l = Get-Content -LiteralPath $env:M_CARBINE_SELF; $i = [array]::IndexOf($l, 'exit /b'); Invoke-Expression ($l[($i + 1)..($l.Length - 1)] -join [char]10)"
pause
exit /b
$ErrorActionPreference = "Stop"
$Here = Split-Path $env:M_CARBINE_SELF -Parent
$Payload = Join-Path $Here "game"
$Sep = [IO.Path]::DirectorySeparatorChar
$SteamBin = "Bin/Win64MasterMasterSteamPGO/"
$OwnDirs = @("Mods/m_carbine", "m4_reload")

function Local-Path([string]$Root, [string]$Relative) {
    Join-Path $Root ($Relative -replace "/", $Sep)
}

function Exe-Dir([string]$Dir) {
    $bin = Join-Path $Dir "Bin"
    if (-not (Test-Path -LiteralPath $bin -PathType Container)) { return $null }
    Get-ChildItem -LiteralPath $bin -Directory | Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName "KingdomCome.exe") } | Select-Object -First 1
}

function Steam-Libraries {
    $steam = (Get-ItemProperty "HKCU:\Software\Valve\Steam" -ErrorAction SilentlyContinue).SteamPath
    if (-not $steam) { return @() }
    $libraries = @($steam)
    $vdf = Join-Path $steam "steamapps/libraryfolders.vdf"
    if (Test-Path -LiteralPath $vdf) {
        foreach ($match in [regex]::Matches((Get-Content -LiteralPath $vdf -Raw), '"path"\s+"([^"]+)"')) {
            $libraries += $match.Groups[1].Value -replace "\\\\", "\"
        }
    }
    $libraries
}

function Find-Game {
    $given = $env:M_CARBINE_GAME_DIR
    if ($given) {
        if (Exe-Dir $given) { return (Resolve-Path -LiteralPath $given).Path }
        throw "No Bin\...\KingdomCome.exe under $given"
    }
    foreach ($library in Steam-Libraries) {
        $candidate = Join-Path $library "steamapps/common/KingdomComeDeliverance2"
        if (Exe-Dir $candidate) { return (Resolve-Path -LiteralPath $candidate).Path }
    }
    Add-Type -AssemblyName System.Windows.Forms
    $dialog = New-Object System.Windows.Forms.FolderBrowserDialog
    $dialog.Description = "Pick the KingdomComeDeliverance2 game folder (the one holding Bin and Mods)"
    while ($dialog.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) {
        if (Exe-Dir $dialog.SelectedPath) { return (Resolve-Path -LiteralPath $dialog.SelectedPath).Path }
        $dialog.Description = "That folder has no Bin\...\KingdomCome.exe. Pick the KingdomComeDeliverance2 game folder."
    }
    throw "No game folder picked."
}

function Note([string]$Line) {
    Add-Content -LiteralPath $Record -Value $Line
}

function Noted([string]$Kind) {
    @(Get-Content -LiteralPath $Record | Where-Object { $_.StartsWith("$Kind ") } | ForEach-Object { $_.Substring($Kind.Length + 1) })
}

function Make-Dir([string]$Dir) {
    $at = $Dir
    while (-not (Test-Path -LiteralPath $at)) {
        Note ("created " + ($at.Substring($Game.Length).TrimStart($Sep) -replace [regex]::Escape($Sep), "/"))
        $at = Split-Path $at -Parent
    }
    New-Item -ItemType Directory -Force -Path $Dir | Out-Null
}

function Install-Mod {
    $exeDir = (Exe-Dir $Game).Name
    New-Item -ItemType File -Force -Path $Record | Out-Null
    foreach ($own in $OwnDirs) {
        $dir = Local-Path $Game $own
        if (Test-Path -LiteralPath $dir) { Remove-Item -LiteralPath $dir -Recurse -Force }
    }
    $files = Get-ChildItem -LiteralPath $Payload -Recurse -File | ForEach-Object { $_.FullName.Substring($Payload.Length + 1) -replace [regex]::Escape($Sep), "/" } | Sort-Object
    foreach ($relative in $files) {
        $placed = $relative
        if ($relative.StartsWith($SteamBin)) { $placed = "Bin/$exeDir/" + $relative.Substring($SteamBin.Length) }
        $target = Local-Path $Game $placed
        if ((Split-Path $relative -Leaf) -eq "dinput8.dll" -and (Test-Path -LiteralPath $target)) {
            Note "kept $placed"
            Write-Host "Kept your $placed (another ASI loader works too)"
            continue
        }
        Make-Dir (Split-Path $target -Parent)
        Copy-Item -LiteralPath (Local-Path $Payload $relative) -Destination $target -Force
        Note "installed $placed"
    }
}

function Restore-Game {
    foreach ($relative in Noted "installed") {
        $target = Local-Path $Game $relative
        if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target }
    }
    foreach ($relative in Noted "created" | Sort-Object Length -Descending) {
        $dir = Local-Path $Game $relative
        if ((Test-Path -LiteralPath $dir -PathType Container) -and -not (Get-ChildItem -LiteralPath $dir -Force)) { Remove-Item -LiteralPath $dir }
    }
    Remove-Item -LiteralPath $Record -Force
}

try {
    if (Get-Process -Name "KingdomCome" -ErrorAction SilentlyContinue) { throw "Close Kingdom Come: Deliverance II first." }
    $Game = Find-Game
    $Record = Join-Path $Game "m_carbine_install.txt"
    Write-Host "Game folder: $Game"
    if (Test-Path -LiteralPath $Record) {
        Write-Host -NoNewline "Henry's Come to See Us! is installed. Update it (u) or remove it (r)? "
        $answer = [Console]::ReadLine()
        if ($answer -eq "r") {
            Restore-Game
            Write-Host "Henry's Come to See Us! removed."
            exit 0
        }
        if ($answer -ne "u") { throw "Nothing changed." }
        Restore-Game
    }
    try {
        Install-Mod
    } catch {
        Write-Host "Install failed: $($_.Exception.Message). Removing what it copied."
        Restore-Game
        throw
    }
    Write-Host "Henry's Come to See Us! installed. Run install_windows.bat again to update or remove it."
    exit 0
} catch {
    Write-Host "Error: $($_.Exception.Message)"
    exit 1
}
