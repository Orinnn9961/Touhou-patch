param(
    [ValidateSet("Apply", "Restore")]
    [string]$Action = "Apply",

    [Parameter(Mandatory = $true)]
    [string]$GameExe
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$originalSha256 = "327C4E923D7057D80D3057AED1E60389F4B560FABCEC73C9DBB956EA68EC52AB"
$patchedSha256 = "C2565B2A68FEFBB7614BB2D055C8B54ADA07ADD63343BF1BA52EB403EB235DB6"
$patches = @(
    [pscustomobject]@{
        Offset = 0x29DC8
        Original = [byte[]](0x00, 0x00, 0xA0, 0x3F)
        Patched = [byte[]](0x00, 0x00, 0x00, 0x00)
    },
    [pscustomobject]@{
        Offset = 0x29DD5
        Original = [byte[]](0x00, 0x00, 0xA0, 0x3F)
        Patched = [byte[]](0x00, 0x00, 0x00, 0x00)
    }
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

function Assert-PatchBytes([byte[]]$Data, [string]$Member, [string]$Description) {
    foreach ($patch in $patches) {
        if (-not (Test-Bytes $Data $patch.Offset $patch.$Member)) {
            throw "$Description bytes were not found at file offset 0x$($patch.Offset.ToString('X'))."
        }
    }
}

function Apply-PatchBytes([byte[]]$Data, [string]$Member) {
    foreach ($patch in $patches) {
        [Array]::Copy($patch.$Member, 0, $Data, $patch.Offset, $patch.$Member.Length)
    }
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
            throw "Unsupported th06.exe. Expected the supported Japanese 1.02h-based executable. Current SHA-256: $currentHash"
        }

        $data = [IO.File]::ReadAllBytes($resolvedExe)
        Assert-PatchBytes $data "Original" "Original hitbox"

        if (Test-Path -LiteralPath $backupPath) {
            if ((Get-Sha256 $backupPath) -ne $originalSha256) {
                throw "A backup file already exists but is not the supported original executable: $backupPath"
            }
        }
        else {
            Copy-Item -LiteralPath $resolvedExe -Destination $backupPath
        }

        Apply-PatchBytes $data "Patched"
        Write-VerifiedFile $resolvedExe $data $patchedSha256
        Write-Host "Patch applied successfully."
        Write-Host "Reimu and Marisa hitbox half-widths are now 0.0 on both axes."
        Write-Host "Backup: $backupPath"
    }
    else {
        if ($currentHash -eq $originalSha256) {
            Write-Host "The executable is already the supported original Japanese version."
            exit 0
        }
        if ($currentHash -ne $patchedSha256) {
            throw "This file is neither the supported original nor this patch's output. Current SHA-256: $currentHash"
        }

        $data = [IO.File]::ReadAllBytes($resolvedExe)
        Assert-PatchBytes $data "Patched" "Zero-hitbox"
        Apply-PatchBytes $data "Original"
        Write-VerifiedFile $resolvedExe $data $originalSha256
        Write-Host "Original hitbox half-widths restored successfully."
    }
}
catch {
    Write-Host "ERROR: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
