<#
LumaShot release-update signing tool (ECDSA P-256 via Windows CNG).

  NewKey        create the private key (DPAPI-protected to the current Windows user)
  PublicKey     print the public key as the C++ array embedded in src/update/public_key.h
  Sign          write a signed update manifest for an installer
  Verify        check a manifest signature and (optionally) the installer hash
  ExportBackup  write the raw private key (base64) for offline backup
  ImportBackup  restore the private key from such a backup

The key never lives in the repository. Default path:
  %USERPROFILE%\.lumashot\release-signing-key
#>
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('NewKey', 'PublicKey', 'Sign', 'Verify', 'ExportBackup', 'ImportBackup')]
    [string]$Action,
    [string]$KeyPath = (Join-Path $env:USERPROFILE '.lumashot\release-signing-key'),
    [string]$Version,
    [string]$Tag,
    [string]$Installer,
    [string]$NotesFile,
    [string[]]$Mirror = @('https://gh-proxy.com/', 'https://ghfast.top/', 'https://ghproxy.net/', 'https://gh.llkk.cc/', 'https://gh.zwy.one/'),
    [string]$Output,
    [string]$Manifest,
    [string]$BackupPath
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Core
Add-Type -AssemblyName System.Security

$Magic = 'LUMASHOT-SIGNING-KEY-1'
$Entropy = [Text.Encoding]::UTF8.GetBytes('LumaShot release signing')
$Utf8 = New-Object Text.UTF8Encoding($false)

function Protect-KeyDirectory([string]$Directory) {
    if (-not (Test-Path $Directory)) { New-Item -ItemType Directory -Path $Directory | Out-Null }
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent().Name
    & icacls.exe $Directory /inheritance:r /grant:r "${identity}:(OI)(CI)F" | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "icacls failed for $Directory" }
}

function Save-PrivateBlob([byte[]]$Blob) {
    Protect-KeyDirectory (Split-Path -Parent $KeyPath)
    $sealed = [Security.Cryptography.ProtectedData]::Protect($Blob, $Entropy, 'CurrentUser')
    [IO.File]::WriteAllText($KeyPath, "$Magic`n" + [Convert]::ToBase64String($sealed) + "`n", $Utf8)
}

function Read-PrivateBlob {
    if (-not (Test-Path $KeyPath)) { throw "Signing key not found: $KeyPath (run -Action NewKey or ImportBackup)" }
    $lines = [IO.File]::ReadAllText($KeyPath).Trim() -split "`n"
    if ($lines[0].Trim() -ne $Magic) { throw "Unrecognized key file: $KeyPath" }
    return [Security.Cryptography.ProtectedData]::Unprotect([Convert]::FromBase64String($lines[1].Trim()), $Entropy, 'CurrentUser')
}

function Read-PrivateKey {
    return [Security.Cryptography.CngKey]::Import((Read-PrivateBlob), [Security.Cryptography.CngKeyBlobFormat]::EccPrivateBlob)
}

function Get-PublicBlob([Security.Cryptography.CngKey]$Key) {
    return $Key.Export([Security.Cryptography.CngKeyBlobFormat]::EccPublicBlob)
}

function Test-Signature([byte[]]$PublicBlob, [byte[]]$Data, [byte[]]$Signature) {
    $public = [Security.Cryptography.CngKey]::Import($PublicBlob, [Security.Cryptography.CngKeyBlobFormat]::EccPublicBlob)
    $ecdsa = New-Object Security.Cryptography.ECDsaCng($public)
    $ecdsa.HashAlgorithm = [Security.Cryptography.CngAlgorithm]::Sha256
    return $ecdsa.VerifyData($Data, $Signature)
}

function Escape-Notes([string]$Text) {
    return ((($Text -replace "`r", '').Trim()) -replace '\\', '\\' -replace "`n", '\n')
}

switch ($Action) {
    'NewKey' {
        if (Test-Path $KeyPath) { throw "Refusing to overwrite existing key: $KeyPath" }
        $parameters = New-Object Security.Cryptography.CngKeyCreationParameters
        $parameters.ExportPolicy = [Security.Cryptography.CngExportPolicies]::AllowPlaintextExport
        $key = [Security.Cryptography.CngKey]::Create([Security.Cryptography.CngAlgorithm]::ECDsaP256, $null, $parameters)
        Save-PrivateBlob ($key.Export([Security.Cryptography.CngKeyBlobFormat]::EccPrivateBlob))
        Write-Output "Created $KeyPath"
        Write-Output ("Public key SHA-256: " + ([BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash((Get-PublicBlob $key))) -replace '-', ''))
    }
    'PublicKey' {
        $blob = Get-PublicBlob (Read-PrivateKey)
        $hex = ($blob | ForEach-Object { '0x{0:x2}' -f $_ })
        $rows = for ($i = 0; $i -lt $hex.Count; $i += 12) { '    ' + (($hex[$i..([Math]::Min($i + 11, $hex.Count - 1))]) -join ',') }
        Write-Output ("// BCRYPT_ECCPUBLIC_BLOB (ECS1, P-256), " + $blob.Length + " bytes")
        Write-Output ($rows -join ",`n")
    }
    'Sign' {
        if (-not $Version -or -not $Installer -or -not $Output) { throw 'Sign needs -Version, -Installer and -Output' }
        if ($Version -notmatch '^\d{1,5}\.\d{1,5}\.\d{1,5}$') { throw "Version must be MAJOR.MINOR.PATCH: $Version" }
        if (-not $Tag) { $Tag = "v$Version" }
        $file = Get-Item $Installer
        $hash = (Get-FileHash -Algorithm SHA256 $file.FullName).Hash.ToLowerInvariant()
        $notes = ''
        if ($NotesFile) { $notes = Escape-Notes ([IO.File]::ReadAllText((Resolve-Path $NotesFile), [Text.Encoding]::UTF8)) }
        $lines = @('LumaShot-Update 1', "version=$Version", "tag=$Tag", "file=$($file.Name)", "size=$($file.Length)", "sha256=$hash",
            ('published=' + [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')), "notes=$notes")
        foreach ($m in $Mirror) {
            if ($m -notmatch '^https://[a-z0-9.-]+(/[A-Za-z0-9._~/-]*)?/$') { throw "Mirror must be an https prefix ending in '/': $m" }
            $lines += "mirror=$m"
        }
        $body = $Utf8.GetBytes(($lines -join "`n") + "`n")
        $key = Read-PrivateKey
        $ecdsa = New-Object Security.Cryptography.ECDsaCng($key)
        $ecdsa.HashAlgorithm = [Security.Cryptography.CngAlgorithm]::Sha256
        $signature = $ecdsa.SignData($body)
        if ($signature.Length -ne 64) { throw "Unexpected signature length $($signature.Length)" }
        if (-not (Test-Signature (Get-PublicBlob $key) $body $signature)) { throw 'Self-verification failed' }
        $all = New-Object byte[] 0
        $tail = $Utf8.GetBytes('signature=' + [Convert]::ToBase64String($signature) + "`n")
        $all = $body + $tail
        [IO.File]::WriteAllBytes($Output, [byte[]]$all)
        Write-Output "Signed $Output (version $Version, $($file.Length) bytes, sha256 $hash)"
    }
    'Verify' {
        if (-not $Manifest) { throw 'Verify needs -Manifest' }
        $bytes = [IO.File]::ReadAllBytes($Manifest)
        $text = $Utf8.GetString($bytes)
        $marker = $text.LastIndexOf("`nsignature=")
        if ($marker -lt 0) { throw 'No signature line' }
        $bodyLength = $Utf8.GetByteCount($text.Substring(0, $marker + 1))
        $signature = [Convert]::FromBase64String($text.Substring($marker + 11).Trim())
        $ok = Test-Signature (Get-PublicBlob (Read-PrivateKey)) ([byte[]]$bytes[0..($bodyLength - 1)]) $signature
        Write-Output "signature_valid=$ok"
        if ($Installer) {
            $expected = ([regex]::Match($text, '(?m)^sha256=([0-9a-f]{64})$')).Groups[1].Value
            $actual = (Get-FileHash -Algorithm SHA256 $Installer).Hash.ToLowerInvariant()
            Write-Output "installer_hash_match=$($expected -eq $actual)"
            if ($expected -ne $actual) { exit 2 }
        }
        if (-not $ok) { exit 1 }
    }
    'ExportBackup' {
        if (-not $BackupPath) { throw 'ExportBackup needs -BackupPath' }
        # Imported CNG keys are not re-exportable; the DPAPI payload already is the raw blob.
        $blob = Read-PrivateBlob
        Read-PrivateKey | Out-Null  # proves the blob is a usable P-256 key
        [IO.File]::WriteAllText($BackupPath, "LUMASHOT-SIGNING-KEY-BACKUP-1`n" + [Convert]::ToBase64String($blob) + "`n", $Utf8)
        Write-Output "Wrote plaintext backup to $BackupPath - move it to offline storage and delete this copy."
    }
    'ImportBackup' {
        if (-not $BackupPath) { throw 'ImportBackup needs -BackupPath' }
        if (Test-Path $KeyPath) { throw "Refusing to overwrite existing key: $KeyPath" }
        $lines = [IO.File]::ReadAllText($BackupPath).Trim() -split "`n"
        if ($lines[0].Trim() -ne 'LUMASHOT-SIGNING-KEY-BACKUP-1') { throw 'Unrecognized backup file' }
        $blob = [Convert]::FromBase64String($lines[1].Trim())
        [Security.Cryptography.CngKey]::Import($blob, [Security.Cryptography.CngKeyBlobFormat]::EccPrivateBlob) | Out-Null
        Save-PrivateBlob $blob
        Write-Output "Restored $KeyPath"
    }
}
