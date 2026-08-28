param(
    [ValidateSet("Apply", "Restore")]
    [string]$Action = "Apply",

    [Parameter(Mandatory = $true)]
    [string]$GameExe
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$hitboxOffset = 0xCC1B0
$originalHitboxes = [byte[]](
    0x00, 0x00, 0x00, 0x40,
    0x00, 0x00, 0x40, 0x40,
    0x00, 0x00, 0x40, 0x40,
    0x00, 0x00, 0x40, 0x40
)
$zeroHitboxes = [byte[]](0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)

$legacyV3Patches = @(
    [PSCustomObject]@{ Name = "boss/body circle hit radius"; Offset = 0x30D48; Original = [byte[]](0xF3, 0x0F, 0x10, 0x50, 0x04); Patched = [byte[]](0x0F, 0x57, 0xD2, 0x90, 0x90) },
    [PSCustomObject]@{ Name = "rectangle hit X minimum"; Offset = 0x5501C; Original = [byte[]](0xF3, 0x0F, 0x10, 0xBA, 0xB0, 0xBF, 0x02, 0x00); Patched = [byte[]](0xF3, 0x0F, 0x10, 0xBA, 0x18, 0x06, 0x00, 0x00) },
    [PSCustomObject]@{ Name = "rectangle hit Y minimum"; Offset = 0x5503B; Original = [byte[]](0xF3, 0x0F, 0x10, 0x82, 0xB4, 0xBF, 0x02, 0x00); Patched = [byte[]](0xF3, 0x0F, 0x10, 0x82, 0x1C, 0x06, 0x00, 0x00) },
    [PSCustomObject]@{ Name = "rectangle hit X maximum"; Offset = 0x55048; Original = [byte[]](0x0F, 0x2F, 0xB2, 0xBC, 0xBF, 0x02, 0x00); Patched = [byte[]](0x0F, 0x2F, 0xB2, 0x18, 0x06, 0x00, 0x00) },
    [PSCustomObject]@{ Name = "rectangle hit Y maximum"; Offset = 0x55051; Original = [byte[]](0x0F, 0x2F, 0xAA, 0xC0, 0xBF, 0x02, 0x00); Patched = [byte[]](0x0F, 0x2F, 0xAA, 0x1C, 0x06, 0x00, 0x00) },
    [PSCustomObject]@{ Name = "general circle hit radius"; Offset = 0x55162; Original = [byte[]](0x0F, 0x28, 0xC1); Patched = [byte[]](0x0F, 0x57, 0xC0) },
    [PSCustomObject]@{ Name = "rotated collision A X extent 1"; Offset = 0x55288; Original = [byte[]](0xF3, 0x0F, 0x10, 0x8E, 0xC8, 0xBF, 0x02, 0x00); Patched = [byte[]](0x0F, 0x57, 0xC9, 0x90, 0x90, 0x90, 0x90, 0x90) },
    [PSCustomObject]@{ Name = "rotated collision A Y extent 1"; Offset = 0x552A9; Original = [byte[]](0xF3, 0x0F, 0x10, 0xAE, 0xCC, 0xBF, 0x02, 0x00); Patched = [byte[]](0x0F, 0x57, 0xED, 0x90, 0x90, 0x90, 0x90, 0x90) },
    [PSCustomObject]@{ Name = "rotated collision A Y extent 2"; Offset = 0x552BF; Original = [byte[]](0xF3, 0x0F, 0x10, 0x86, 0xCC, 0xBF, 0x02, 0x00); Patched = [byte[]](0x0F, 0x57, 0xC0, 0x90, 0x90, 0x90, 0x90, 0x90) },
    [PSCustomObject]@{ Name = "rotated collision A X extent 2"; Offset = 0x552E6; Original = [byte[]](0xF3, 0x0F, 0x10, 0x86, 0xC8, 0xBF, 0x02, 0x00); Patched = [byte[]](0x0F, 0x57, 0xC0, 0x90, 0x90, 0x90, 0x90, 0x90) },
    [PSCustomObject]@{ Name = "rotated collision B X extent 1"; Offset = 0x55463; Original = [byte[]](0xF3, 0x0F, 0x10, 0x8E, 0xC8, 0xBF, 0x02, 0x00); Patched = [byte[]](0x0F, 0x57, 0xC9, 0x90, 0x90, 0x90, 0x90, 0x90) },
    [PSCustomObject]@{ Name = "rotated collision B Y extent 1"; Offset = 0x55475; Original = [byte[]](0xF3, 0x0F, 0x10, 0xAE, 0xCC, 0xBF, 0x02, 0x00); Patched = [byte[]](0x0F, 0x57, 0xED, 0x90, 0x90, 0x90, 0x90, 0x90) },
    [PSCustomObject]@{ Name = "rotated collision B Y extent 2"; Offset = 0x5548D; Original = [byte[]](0xF3, 0x0F, 0x10, 0x86, 0xCC, 0xBF, 0x02, 0x00); Patched = [byte[]](0x0F, 0x57, 0xC0, 0x90, 0x90, 0x90, 0x90, 0x90) },
    [PSCustomObject]@{ Name = "rotated collision B X extent 2"; Offset = 0x554B4; Original = [byte[]](0xF3, 0x0F, 0x10, 0x86, 0xC8, 0xBF, 0x02, 0x00); Patched = [byte[]](0x0F, 0x57, 0xC0, 0x90, 0x90, 0x90, 0x90, 0x90) },
    [PSCustomObject]@{ Name = "rotated collision C X extent 1"; Offset = 0x555E4; Original = [byte[]](0xF3, 0x0F, 0x10, 0x8E, 0xC8, 0xBF, 0x02, 0x00); Patched = [byte[]](0x0F, 0x57, 0xC9, 0x90, 0x90, 0x90, 0x90, 0x90) },
    [PSCustomObject]@{ Name = "rotated collision C Y extent 1"; Offset = 0x55618; Original = [byte[]](0xF3, 0x0F, 0x10, 0x86, 0xCC, 0xBF, 0x02, 0x00); Patched = [byte[]](0x0F, 0x57, 0xC0, 0x90, 0x90, 0x90, 0x90, 0x90) },
    [PSCustomObject]@{ Name = "rotated collision C X extent 2"; Offset = 0x55620; Original = [byte[]](0xF3, 0x0F, 0x10, 0xB6, 0xC8, 0xBF, 0x02, 0x00); Patched = [byte[]](0x0F, 0x57, 0xF6, 0x90, 0x90, 0x90, 0x90, 0x90) },
    [PSCustomObject]@{ Name = "rotated collision C Y extent 2"; Offset = 0x5564E; Original = [byte[]](0xF3, 0x0F, 0x10, 0x86, 0xCC, 0xBF, 0x02, 0x00); Patched = [byte[]](0x0F, 0x57, 0xC0, 0x90, 0x90, 0x90, 0x90, 0x90) }
)

$patches = @(
    [PSCustomObject]@{ Name = "boss/body circle inner radius"; Offset = 0x30D6D; Original = [byte[]](0x0F, 0x28, 0xC2); Patched = [byte[]](0x0F, 0x57, 0xC0) },
    [PSCustomObject]@{ Name = "rectangle hit X minimum comparison"; Offset = 0x5502E; Original = [byte[]](0x0F, 0x2F, 0xF8); Patched = [byte[]](0x0F, 0x2F, 0xE0) },
    [PSCustomObject]@{ Name = "rectangle hit Y minimum"; Offset = 0x5503B; Original = [byte[]](0xF3, 0x0F, 0x10, 0x82, 0xB4, 0xBF, 0x02, 0x00); Patched = [byte[]](0xF3, 0x0F, 0x10, 0x82, 0x1C, 0x06, 0x00, 0x00) },
    [PSCustomObject]@{ Name = "rectangle hit X maximum"; Offset = 0x55048; Original = [byte[]](0x0F, 0x2F, 0xB2, 0xBC, 0xBF, 0x02, 0x00); Patched = [byte[]](0x0F, 0x2F, 0xB2, 0x18, 0x06, 0x00, 0x00) },
    [PSCustomObject]@{ Name = "rectangle hit Y maximum"; Offset = 0x55051; Original = [byte[]](0x0F, 0x2F, 0xAA, 0xC0, 0xBF, 0x02, 0x00); Patched = [byte[]](0x0F, 0x2F, 0xAA, 0x1C, 0x06, 0x00, 0x00) },
    [PSCustomObject]@{ Name = "general circle inner radius"; Offset = 0x55162; Original = [byte[]](0x0F, 0x28, 0xC1); Patched = [byte[]](0x0F, 0x57, 0xC0) },
    [PSCustomObject]@{ Name = "rotated collision A inner Y extent 1"; Offset = 0x55342; Original = [byte[]](0xF3, 0x0F, 0x10, 0x9E, 0xCC, 0xBF, 0x02, 0x00); Patched = [byte[]](0x0F, 0x57, 0xDB, 0x90, 0x90, 0x90, 0x90, 0x90) },
    [PSCustomObject]@{ Name = "rotated collision A inner X extent 1"; Offset = 0x5534D; Original = [byte[]](0xF3, 0x0F, 0x5C, 0x86, 0xC8, 0xBF, 0x02, 0x00); Patched = [byte[]](0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90) },
    [PSCustomObject]@{ Name = "rotated collision A inner X extent 2"; Offset = 0x55355; Original = [byte[]](0xF3, 0x0F, 0x58, 0xA6, 0xC8, 0xBF, 0x02, 0x00); Patched = [byte[]](0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90) },
    [PSCustomObject]@{ Name = "rotated collision A inner Y extent 2"; Offset = 0x55364; Original = [byte[]](0xF3, 0x0F, 0x5C, 0xAE, 0xCC, 0xBF, 0x02, 0x00); Patched = [byte[]](0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90) },
    [PSCustomObject]@{ Name = "rotated collision B inner Y extent 1"; Offset = 0x5550A; Original = [byte[]](0xF3, 0x0F, 0x10, 0x9E, 0xCC, 0xBF, 0x02, 0x00); Patched = [byte[]](0x0F, 0x57, 0xDB, 0x90, 0x90, 0x90, 0x90, 0x90) },
    [PSCustomObject]@{ Name = "rotated collision B inner X extent 1"; Offset = 0x55515; Original = [byte[]](0xF3, 0x0F, 0x5C, 0x86, 0xC8, 0xBF, 0x02, 0x00); Patched = [byte[]](0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90) },
    [PSCustomObject]@{ Name = "rotated collision B inner X extent 2"; Offset = 0x55526; Original = [byte[]](0xF3, 0x0F, 0x10, 0xA6, 0xC8, 0xBF, 0x02, 0x00); Patched = [byte[]](0x0F, 0x57, 0xE4, 0x90, 0x90, 0x90, 0x90, 0x90) },
    [PSCustomObject]@{ Name = "rotated collision B inner Y extent 2"; Offset = 0x5552E; Original = [byte[]](0xF3, 0x0F, 0x5C, 0xAE, 0xCC, 0xBF, 0x02, 0x00); Patched = [byte[]](0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90) },
    [PSCustomObject]@{ Name = "rotated collision C inner Y extent 1"; Offset = 0x556CC; Original = [byte[]](0xF3, 0x0F, 0x10, 0x9E, 0xCC, 0xBF, 0x02, 0x00); Patched = [byte[]](0x0F, 0x57, 0xDB, 0x90, 0x90, 0x90, 0x90, 0x90) },
    [PSCustomObject]@{ Name = "rotated collision C inner X extent 1"; Offset = 0x556D7; Original = [byte[]](0xF3, 0x0F, 0x5C, 0x86, 0xC8, 0xBF, 0x02, 0x00); Patched = [byte[]](0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90) },
    [PSCustomObject]@{ Name = "rotated collision C inner X extent 2"; Offset = 0x556DF; Original = [byte[]](0xF3, 0x0F, 0x58, 0xA6, 0xC8, 0xBF, 0x02, 0x00); Patched = [byte[]](0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90) },
    [PSCustomObject]@{ Name = "rotated collision C inner Y extent 2"; Offset = 0x556EE; Original = [byte[]](0xF3, 0x0F, 0x5C, 0xB6, 0xCC, 0xBF, 0x02, 0x00); Patched = [byte[]](0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90) }
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
        throw "The target is not a compatible 32-bit TH15 executable."
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
        throw "The target does not use the compatible TH15 1.00b image layout."
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

    $patchEnd = 0x556EE + 8
    if (($textVa -ne 0x1000) -or ($textRaw -ne 0x400) -or
        ($textRawSize -lt ($patchEnd - $textRaw)) -or ($dataVa -ne 0xDE000)) {
        throw "The target's code/data mapping is not compatible with TH15 1.00b."
    }
}

function Get-PatchSetState([byte[]]$Data, [object[]]$PatchSet) {
    $originalCount = 0
    $patchedCount = 0
    foreach ($patch in $PatchSet) {
        if (Test-Bytes $Data $patch.Offset $patch.Original) {
            $originalCount++
        }
        elseif (Test-Bytes $Data $patch.Offset $patch.Patched) {
            $patchedCount++
        }
        else {
            throw "The compatible TH15 layout was found, but '$($patch.Name)' is occupied by another modification at file offset 0x$($patch.Offset.ToString('X'))."
        }
    }

    if ($originalCount -eq $PatchSet.Count) { return "Original" }
    if ($patchedCount -eq $PatchSet.Count) { return "Patched" }
    return "Mixed"
}

function Get-PatchLayoutState([byte[]]$Data) {
    $correctedState = Get-PatchSetState $Data $patches
    $legacyV3State = Get-PatchSetState $Data $legacyV3Patches

    if ($correctedState -eq "Patched") { return "Corrected" }
    if ($legacyV3State -eq "Patched") { return "LegacyV3" }
    if (($correctedState -eq "Original") -and ($legacyV3State -eq "Original")) { return "Original" }
    return "Mixed"
}

function Set-PatchBytes([byte[]]$Data, [object[]]$PatchSet, [string]$Side) {
    foreach ($patch in $PatchSet) {
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
    $patchState = Get-PatchLayoutState $data

    if ($Action -eq "Apply") {
        if ($patchState -eq "Corrected") {
            Write-Host "The corrected hit-only zero-radius patch is already applied."
            exit 0
        }
        if ($patchState -eq "Mixed") {
            throw "A partial hitbox patch was found. Restore the matching patch version before continuing."
        }

        if ($patchState -eq "LegacyV3") {
            Set-PatchBytes $data $legacyV3Patches "Original"
            Write-Host "Migrating the flawed v3.0 graze-gate patch to the corrected patch."
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

        Set-PatchBytes $data $patches "Patched"
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

        $activePatchSet = if ($patchState -eq "LegacyV3") { $legacyV3Patches } else { $patches }
        Set-PatchBytes $data $activePatchSet "Original"
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
