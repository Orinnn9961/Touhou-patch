param(
    [ValidateSet("Apply", "Restore")]
    [string]$Action = "Apply",

    [Parameter(Mandatory = $true)]
    [string]$GameExe
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# v3.0: hit-only collision patch with original graze geometry.

$hitboxOffset = 0xA60A8
$originalHitboxes = [byte[]](
    0x00, 0x00, 0x00, 0x40,
    0x00, 0x00, 0x60, 0x40
)
$zeroHitboxes = [byte[]](0, 0, 0, 0, 0, 0, 0, 0)

$payload = [byte[]](
    0x6A, 0x00, 0xEB, 0x06, 0x6A, 0x01, 0xEB, 0x02, 0x6A, 0x02, 0x55, 0x8B,
    0xEC, 0x56, 0x83, 0xEC, 0x10, 0x89, 0x45, 0xF8, 0x8B, 0x0D, 0xB4, 0x8E,
    0x4A, 0x00, 0x89, 0x4D, 0xF4, 0xE8, 0x72, 0x00, 0x00, 0x00, 0x83, 0xF8,
    0x01, 0x75, 0x64, 0x8B, 0x4D, 0xF4, 0x8B, 0x91, 0xE4, 0x08, 0x00, 0x00,
    0x89, 0x55, 0xF0, 0x8B, 0x91, 0xE8, 0x08, 0x00, 0x00, 0x89, 0x55, 0xEC,
    0xC7, 0x81, 0xE4, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC7, 0x81,
    0xE8, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE8, 0x3F, 0x00, 0x00,
    0x00, 0x8B, 0x4D, 0xF4, 0x8B, 0x55, 0xF0, 0x89, 0x91, 0xE4, 0x08, 0x00,
    0x00, 0x8B, 0x55, 0xEC, 0x89, 0x91, 0xE8, 0x08, 0x00, 0x00, 0x83, 0xF8,
    0x01, 0x74, 0x07, 0xB8, 0x02, 0x00, 0x00, 0x00, 0xEB, 0x15, 0x83, 0x7D,
    0x04, 0x01, 0x74, 0x0A, 0x8B, 0x45, 0xF4, 0xBA, 0x90, 0x2A, 0x43, 0x00,
    0xFF, 0xD2, 0xB8, 0x01, 0x00, 0x00, 0x00, 0x8D, 0x65, 0xFC, 0x5E, 0x5D,
    0x59, 0xC2, 0x0C, 0x00, 0xFF, 0x75, 0x14, 0xFF, 0x75, 0x10, 0xFF, 0x75,
    0x0C, 0xC6, 0x05, 0x40, 0xAE, 0x4C, 0x00, 0x01, 0x8B, 0x45, 0xF8, 0x8B,
    0x55, 0x04, 0x85, 0xD2, 0x74, 0x0C, 0x83, 0xFA, 0x01, 0x74, 0x0E, 0xE8,
    0x2E, 0x00, 0x00, 0x00, 0xEB, 0x0C, 0xE8, 0x0F, 0x00, 0x00, 0x00, 0xEB,
    0x05, 0xE8, 0x14, 0x00, 0x00, 0x00, 0xC6, 0x05, 0x40, 0xAE, 0x4C, 0x00,
    0x00, 0xC3, 0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x68, 0x76, 0x20, 0x43,
    0x00, 0xC3, 0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x68, 0xF6, 0x22, 0x43,
    0x00, 0xC3, 0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x68, 0x26, 0x25, 0x43,
    0x00, 0xC3, 0x80, 0x3D, 0x40, 0xAE, 0x4C, 0x00, 0x00, 0x75, 0x06, 0x68,
    0x90, 0x2A, 0x43, 0x00, 0xC3, 0xC3
)

$patches = @(
    [PSCustomObject]@{ Name = "text payload storage"; Offset = 0x220; Original = [byte[]](0x15, 0x9E, 0x08, 0x00); Patched = [byte[]](0x17, 0x9F, 0x08, 0x00) },
    [PSCustomObject]@{ Name = "data probe storage"; Offset = 0x270; Original = [byte[]](0x40, 0x8E, 0x02, 0x00); Patched = [byte[]](0x44, 0x8E, 0x02, 0x00) },
    [PSCustomObject]@{ Name = "rectangle hit X minimum"; Offset = 0x3123F; Original = [byte[]](0xCC); Patched = [byte[]](0x7C) },
    [PSCustomObject]@{ Name = "rectangle hit Y minimum"; Offset = 0x31255; Original = [byte[]](0xD0); Patched = [byte[]](0x80) },
    [PSCustomObject]@{ Name = "rectangle hit X maximum"; Offset = 0x3126C; Original = [byte[]](0xD8); Patched = [byte[]](0x7C) },
    [PSCustomObject]@{ Name = "rectangle hit Y maximum"; Offset = 0x3127F; Original = [byte[]](0xDC); Patched = [byte[]](0x80) },
    [PSCustomObject]@{ Name = "circle hit radius"; Offset = 0x313A2; Original = [byte[]](0xD9, 0x40, 0x04); Patched = [byte[]](0xD9, 0xEE, 0x90) },
    [PSCustomObject]@{ Name = "rotated collision A hook"; Offset = 0x31470; Original = [byte[]](0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8); Patched = [byte[]](0xE9, 0xA0, 0x8D, 0x05, 0x00, 0x90) },
    [PSCustomObject]@{ Name = "rotated collision A hit gate"; Offset = 0x3167B; Original = [byte[]](0xE8, 0x10, 0x08, 0x00, 0x00); Patched = [byte[]](0xE8, 0x87, 0x8C, 0x05, 0x00) },
    [PSCustomObject]@{ Name = "rotated collision B hook"; Offset = 0x316F0; Original = [byte[]](0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8); Patched = [byte[]](0xE9, 0x24, 0x8B, 0x05, 0x00, 0x90) },
    [PSCustomObject]@{ Name = "rotated collision C hook"; Offset = 0x31920; Original = [byte[]](0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8); Patched = [byte[]](0xE9, 0xF8, 0x88, 0x05, 0x00, 0x90) },
    [PSCustomObject]@{ Name = "rotated collision C hit gate"; Offset = 0x31B3F; Original = [byte[]](0xE8, 0x4C, 0x03, 0x00, 0x00); Patched = [byte[]](0xE8, 0xC3, 0x87, 0x05, 0x00) },
    [PSCustomObject]@{ Name = "hit-only collision payload"; Offset = 0x8A215; Original = [byte[]](New-Object byte[] $payload.Length); Patched = $payload }
)

function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

function Get-DataSha256([byte[]]$Data) {
    $sha256 = [Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString($sha256.ComputeHash($Data))).Replace("-", "")
    }
    finally {
        $sha256.Dispose()
    }
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

function Assert-CompatiblePe([byte[]]$Data) {
    if (($Data.Length -lt ($hitboxOffset + $originalHitboxes.Length)) -or
        ([Text.Encoding]::ASCII.GetString($Data, 0, 2) -ne "MZ")) {
        throw "The target is not a compatible 32-bit TH11 executable."
    }

    $peOffset = [BitConverter]::ToInt32($Data, 0x3C)
    if (($peOffset -lt 0x40) -or ($peOffset + 0xF8 -gt $Data.Length) -or
        ([Text.Encoding]::ASCII.GetString($Data, $peOffset, 4) -ne "PE`0`0")) {
        throw "The target has an invalid PE header."
    }
    if ([BitConverter]::ToUInt16($Data, $peOffset + 4) -ne 0x14C) {
        throw "The target is not an x86 executable."
    }

    $optionalHeader = $peOffset + 24
    if (([BitConverter]::ToUInt16($Data, $optionalHeader) -ne 0x10B) -or
        ([BitConverter]::ToUInt32($Data, $optionalHeader + 28) -ne 0x400000)) {
        throw "The target does not use the compatible TH11 1.00a image layout."
    }

    $sectionCount = [BitConverter]::ToUInt16($Data, $peOffset + 6)
    $optionalSize = [BitConverter]::ToUInt16($Data, $peOffset + 20)
    $sectionTable = $optionalHeader + $optionalSize
    $textVa = -1
    $textRaw = -1
    $textRawSize = -1
    $dataVa = -1
    for ($i = 0; $i -lt $sectionCount; $i++) {
        $header = $sectionTable + $i * 40
        if ($header + 40 -gt $Data.Length) {
            throw "The target has an invalid PE section table."
        }
        $name = [Text.Encoding]::ASCII.GetString($Data, $header, 8).TrimEnd([char]0)
        if ($name -eq ".text") {
            $textVa = [BitConverter]::ToInt32($Data, $header + 12)
            $textRawSize = [BitConverter]::ToInt32($Data, $header + 16)
            $textRaw = [BitConverter]::ToInt32($Data, $header + 20)
        }
        elseif ($name -eq ".data") {
            $dataVa = [BitConverter]::ToInt32($Data, $header + 12)
        }
    }

    $payloadEnd = 0x8A215 + $payload.Length
    if (($textVa -ne 0x1000) -or ($textRaw -ne 0x400) -or
        ($textRawSize -lt ($payloadEnd - $textRaw)) -or ($dataVa -ne 0xA2000)) {
        throw "The target's code/data mapping is not compatible with TH11 1.00a."
    }
}

function Get-PatchState([byte[]]$Data) {
    $originalCount = 0
    $patchedCount = 0
    foreach ($patch in $patches) {
        if (Test-Bytes $Data $patch.Offset $patch.Original) {
            $originalCount++
        }
        elseif (Test-Bytes $Data $patch.Offset $patch.Patched) {
            $patchedCount++
        }
        else {
            throw "The compatible TH11 layout was found, but '$($patch.Name)' is occupied by another modification at file offset 0x$($patch.Offset.ToString('X'))."
        }
    }

    if ($originalCount -eq $patches.Count) { return "Original" }
    if ($patchedCount -eq $patches.Count) { return "Patched" }
    return "Mixed"
}

function Set-PatchBytes([byte[]]$Data, [string]$Side) {
    foreach ($patch in $patches) {
        $replacement = if ($Side -eq "Original") { $patch.Original } else { $patch.Patched }
        Replace-Bytes $Data $patch.Offset $replacement
    }
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
    $data = [IO.File]::ReadAllBytes($resolvedExe)
    Assert-CompatiblePe $data
    $patchState = Get-PatchState $data

    if ($Action -eq "Apply") {
        if ($patchState -eq "Patched") {
            Write-Host "The compatible hit-only zero-radius patch is already applied."
            exit 0
        }
        if ($patchState -eq "Mixed") {
            throw "A partial hitbox patch was found. Restore the matching patch version before continuing."
        }

        if (Test-Bytes $data $hitboxOffset $zeroHitboxes) {
            Replace-Bytes $data $hitboxOffset $originalHitboxes
            Write-Host "Migrating the legacy radius-table patch to the hit-only patch."
        }

        $prePatchHash = Get-DataSha256 $data
        $backupPath = "$resolvedExe.hitbox-zero.prepatch.$prePatchHash.bak"
        if (Test-Path -LiteralPath $backupPath) {
            $backupHash = Get-Sha256 $backupPath
            if ($backupHash -ne $prePatchHash) {
                throw "A backup for a different pre-patch executable already exists: $backupPath"
            }
        }
        else {
            [IO.File]::WriteAllBytes($backupPath, $data)
            if ((Get-Sha256 $backupPath) -ne $prePatchHash) {
                throw "The pre-patch backup could not be verified: $backupPath"
            }
        }

        Set-PatchBytes $data "Patched"
        $patchedHash = Get-DataSha256 $data
        Write-VerifiedFile $resolvedExe $data $patchedHash
        Write-Host "Patch applied successfully."
        Write-Host "Hit detection now uses zero player radius while original graze detection is preserved."
        Write-Host "Patched SHA-256: $patchedHash"
        Write-Host "Backup: $backupPath"
    }
    else {
        if ($patchState -eq "Mixed") {
            throw "A partial hitbox patch was found. Automatic restore is unsafe."
        }
        if ($patchState -eq "Original") {
            if (Test-Bytes $data $hitboxOffset $zeroHitboxes) {
                Replace-Bytes $data $hitboxOffset $originalHitboxes
                $restoredHash = Get-DataSha256 $data
                Write-VerifiedFile $resolvedExe $data $restoredHash
                Write-Host "The legacy radius-table patch was restored."
                Write-Host "Restored SHA-256: $restoredHash"
                exit 0
            }
            Write-Host "The compatible hit-only zero-radius patch is not applied."
            exit 0
        }

        Set-PatchBytes $data "Original"
        $restoredHash = Get-DataSha256 $data
        Write-VerifiedFile $resolvedExe $data $restoredHash
        Write-Host "This patch's collision changes were restored successfully."
        Write-Host "Restored SHA-256: $restoredHash"
        $backupPath = "$resolvedExe.hitbox-zero.prepatch.$restoredHash.bak"
        if (Test-Path -LiteralPath $backupPath) {
            if ((Get-Sha256 $backupPath) -eq $restoredHash) {
                Write-Host "The restored executable matches the pre-patch backup."
            }
            else {
                Write-Host "The backup differs because other bytes changed after patching; it was retained: $backupPath"
            }
        }
    }
}
catch {
    Write-Host "ERROR: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
