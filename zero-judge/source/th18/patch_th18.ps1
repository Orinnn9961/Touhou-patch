param(
    [ValidateSet("Apply", "Restore")]
    [string]$Action = "Apply",

    [Parameter(Mandatory = $true)]
    [string]$GameExe
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$originalSha256 = "5AEB74B19939A29A8CF06E4FE7777AAF9139E93478507E039F43318171020E9F"
$patchedSha256 = "3EB820077368E5FBCBCEBC5A3B1D57C8A6C0079EB33732389236BE81A2BC5637"
$patchOffset = 0x5C1D0
$originalBytes = [byte[]](
    0xF3, 0x0F, 0x10, 0x89, 0xA0, 0x79, 0x04, 0x00,
    0xEB, 0x08,
    0xF3, 0x0F, 0x10, 0x89, 0x9C, 0x79, 0x04, 0x00
)
$patchedBytes = [byte[]](
    0x0F, 0x57, 0xC9, 0x90, 0x90, 0x90, 0x90, 0x90,
    0xEB, 0x08,
    0x0F, 0x57, 0xC9, 0x90, 0x90, 0x90, 0x90, 0x90
)

function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

function Test-Bytes([byte[]]$Data, [int]$Offset, [byte[]]$Expected) {
    if ($Data.Length -lt ($Offset + $Expected.Length)) {
        return $false
    }

    for ($i = 0; $i -lt $Expected.Length; $i++) {
        if ($Data[$Offset + $i] -ne $Expected[$i]) {
            return $false
        }
    }
    return $true
}

function Replace-Bytes([byte[]]$Data, [int]$Offset, [byte[]]$Replacement) {
    [Array]::Copy($Replacement, 0, $Data, $Offset, $Replacement.Length)
}

function Write-VerifiedFile([string]$Path, [byte[]]$Data, [string]$ExpectedHash) {
    $directory = Split-Path -Parent $Path
    $tempPath = Join-Path $directory ([IO.Path]::GetRandomFileName())
    $replaceBackupPath = Join-Path $directory ([IO.Path]::GetRandomFileName())

    try {
        [IO.File]::WriteAllBytes($tempPath, $Data)
        $actualHash = Get-Sha256 $tempPath
        if ($actualHash -ne $ExpectedHash) {
            throw "Generated file hash mismatch. No changes were applied."
        }

        [IO.File]::Replace($tempPath, $Path, $replaceBackupPath)
    }
    finally {
        if (Test-Path -LiteralPath $tempPath) {
            Remove-Item -LiteralPath $tempPath -Force
        }
        if (Test-Path -LiteralPath $replaceBackupPath) {
            Remove-Item -LiteralPath $replaceBackupPath -Force
        }
    }
}

try {
    $resolvedExe = (Resolve-Path -LiteralPath $GameExe).Path
    $currentHash = Get-Sha256 $resolvedExe
    $backupPath = "$resolvedExe.hitbox-zero.original.bak"

    if ($Action -eq "Apply") {
        if ($currentHash -eq $patchedSha256) {
            Write-Host "The zero-hitbox patch is already applied."
            exit 0
        }
        if ($currentHash -ne $originalSha256) {
            throw "Unsupported th18.exe. Expected the original Japanese 1.00a executable. Current SHA-256: $currentHash"
        }

        $data = [IO.File]::ReadAllBytes($resolvedExe)
        if (-not (Test-Bytes $data $patchOffset $originalBytes)) {
            throw "Original hitbox-selection code was not found at file offset 0x5C1D0."
        }

        if (Test-Path -LiteralPath $backupPath) {
            $backupHash = Get-Sha256 $backupPath
            if ($backupHash -ne $originalSha256) {
                throw "A backup file already exists but is not an original Japanese 1.00a executable: $backupPath"
            }
        }
        else {
            Copy-Item -LiteralPath $resolvedExe -Destination $backupPath
        }

        Replace-Bytes $data $patchOffset $patchedBytes
        Write-VerifiedFile $resolvedExe $data $patchedSha256
        Write-Host "Patch applied successfully."
        Write-Host "Reimu, Marisa, Sakuya, and Sanae hitbox radii are now 0.0."
        Write-Host "Backup: $backupPath"
    }
    else {
        if ($currentHash -eq $originalSha256) {
            Write-Host "The executable is already the original Japanese 1.00a version."
            exit 0
        }
        if ($currentHash -ne $patchedSha256) {
            throw "This file is neither the supported original nor this patch's output. Current SHA-256: $currentHash"
        }

        $data = [IO.File]::ReadAllBytes($resolvedExe)
        if (-not (Test-Bytes $data $patchOffset $patchedBytes)) {
            throw "Zero-hitbox code was not found at file offset 0x5C1D0."
        }

        Replace-Bytes $data $patchOffset $originalBytes
        Write-VerifiedFile $resolvedExe $data $originalSha256
        Write-Host "Original hitbox-selection code restored successfully."
    }
}
catch {
    Write-Host "ERROR: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
