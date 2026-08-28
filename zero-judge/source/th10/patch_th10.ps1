param(
    [ValidateSet("Apply", "Restore")]
    [string]$Action = "Apply",

    [Parameter(Mandatory = $true)]
    [string]$GameExe
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$originalSha256 = "2F14760B6FBBF57549541583283BADB9A19A4222B90F0A146D5AA17F01DC9040"
$patchedSha256 = "C288B519C900F50D063935207BE9214480CBAE40763F9676768889D87625E3B9"
$hitboxOffset = 0x75BB8
$originalHitboxes = [byte[]](
    0x00, 0x00, 0x00, 0x40,
    0x00, 0x00, 0x60, 0x40
)
$zeroHitboxes = [byte[]](0, 0, 0, 0, 0, 0, 0, 0)

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
            throw "Unsupported th10.exe. Expected the original Japanese 1.00a executable. Current SHA-256: $currentHash"
        }

        $data = [IO.File]::ReadAllBytes($resolvedExe)
        if (-not (Test-Bytes $data $hitboxOffset $originalHitboxes)) {
            throw "Original hitbox bytes were not found at file offset 0x75BB8."
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

        Replace-Bytes $data $hitboxOffset $zeroHitboxes
        Write-VerifiedFile $resolvedExe $data $patchedSha256
        Write-Host "Patch applied successfully."
        Write-Host "Reimu and Marisa hitbox radii are now 0.0."
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
        if (-not (Test-Bytes $data $hitboxOffset $zeroHitboxes)) {
            throw "Zero-hitbox bytes were not found at file offset 0x75BB8."
        }

        Replace-Bytes $data $hitboxOffset $originalHitboxes
        Write-VerifiedFile $resolvedExe $data $originalSha256
        Write-Host "Original hitbox radii restored successfully."
    }
}
catch {
    Write-Host "ERROR: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
