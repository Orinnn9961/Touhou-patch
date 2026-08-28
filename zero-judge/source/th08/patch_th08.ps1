param(
    [ValidateSet("Apply", "Restore")]
    [string]$Action = "Apply",

    [Parameter(Mandatory = $true)]
    [string]$GameExe
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$originalSha256 = "330FBDBF58A710829D65277B4F312CFBB38D5448B3DF523E79350B879213D924"
$patchedSha256 = "5E50444BB37F9539D31A18E8EEE0432F0FC18D5ADD890ED4B245D09EA3912228"
$patchOffset = 0x4D5D7
$originalBytes = [byte[]](0xD9, 0x42, 0x0C)
$patchedBytes = [byte[]](0xD9, 0xEE, 0x90)

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

function Write-VerifiedFile([string]$Path, [byte[]]$Data, [string]$ExpectedHash) {
    $directory = Split-Path -Parent $Path
    $tempPath = Join-Path $directory ([IO.Path]::GetRandomFileName())
    $replaceBackupPath = Join-Path $directory ([IO.Path]::GetRandomFileName())

    try {
        [IO.File]::WriteAllBytes($tempPath, $Data)
        if ((Get-Sha256 $tempPath) -ne $ExpectedHash) {
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
            throw "Unsupported th08.exe. Expected the original Japanese 1.00d executable. Current SHA-256: $currentHash"
        }

        $data = [IO.File]::ReadAllBytes($resolvedExe)
        if (-not (Test-Bytes $data $patchOffset $originalBytes)) {
            throw "Original hitbox instruction was not found at file offset 0x4D5D7."
        }

        if (Test-Path -LiteralPath $backupPath) {
            if ((Get-Sha256 $backupPath) -ne $originalSha256) {
                throw "A backup file already exists but is not an original Japanese 1.00d executable: $backupPath"
            }
        }
        else {
            Copy-Item -LiteralPath $resolvedExe -Destination $backupPath
        }

        [Array]::Copy($patchedBytes, 0, $data, $patchOffset, $patchedBytes.Length)
        Write-VerifiedFile $resolvedExe $data $patchedSha256
        Write-Host "Patch applied successfully."
        Write-Host "All team and solo-character hitbox half-widths are now 0.0 on both axes."
        Write-Host "Backup: $backupPath"
    }
    else {
        if ($currentHash -eq $originalSha256) {
            Write-Host "The executable is already the original Japanese 1.00d version."
            exit 0
        }
        if ($currentHash -ne $patchedSha256) {
            throw "This file is neither the supported original nor this patch's output. Current SHA-256: $currentHash"
        }

        $data = [IO.File]::ReadAllBytes($resolvedExe)
        if (-not (Test-Bytes $data $patchOffset $patchedBytes)) {
            throw "Zero-hitbox instruction was not found at file offset 0x4D5D7."
        }

        [Array]::Copy($originalBytes, 0, $data, $patchOffset, $originalBytes.Length)
        Write-VerifiedFile $resolvedExe $data $originalSha256
        Write-Host "Original hitbox loading instruction restored successfully."
    }
}
catch {
    Write-Host "ERROR: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
