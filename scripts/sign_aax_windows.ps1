<#
    Sign the Windows ToneFill AAX with PACE wraptool, interactively, in a Windows VM.

    Why this exists: the native AAX builds fine (locally or in CI), but the Authenticode
    signature must be applied by wraptool running ON WINDOWS. This does it without a paid
    certificate (self-signed cert, like the CI flow) and without a physical iLok (iLok Cloud).

    PREREQUISITES (one-time, in the VM):
      1. iLok License Manager installed (free, from https://www.ilok.com) - provides iloktool
         and the cloud session. Log in once with your iLok account so it's known.
      2. PACE Eden SDK installed (provides wraptool.exe). If you don't have it, grab
         eden-sdk-win64.zip from the tonefill-ara 'sdk-assets' release, extract, and run the
         Eden MSI (msiexec /i <the .msi> /qn ADDLOCAL=ALL).
      3. The UNSIGNED bundle: download artifact 'ToneFill-Windows-AAX-unsigned' from a
         successful 'Build ToneFill AAX - Windows' run, and reconstruct the folder as:
             ToneFill.aaxplugin\Contents\x64\ToneFill.aaxplugin
         (the artifact zip contains Contents\x64\... - just put it inside a ToneFill.aaxplugin folder).

    USAGE (PowerShell):
      .\sign_aax_windows.ps1 -Bundle "C:\path\to\ToneFill.aaxplugin" -PacePassword "<iLok account password>" -Install

    Notes:
      - The plugin ends up PACE-wrapped + self-signed. Windows shows "unknown publisher" - fine;
        Pro Tools validates the PACE wrap, not OS publisher trust.
      - iLok Cloud sessions are one-machine-at-a-time per account: if the session is open on your
        Mac, this opens it on the VM (closing the Mac's).
#>

param(
    [Parameter(Mandatory=$true)] [string] $Bundle,        # path to the ToneFill.aaxplugin folder
    [Parameter(Mandatory=$true)] [string] $PacePassword,  # iLok account password
    [string] $Account = "cactuzz",
    [string] $Wcguid  = "53B16D60-73F3-11F1-B005-005056920FF7",
    [switch] $Cloud   = $true,   # open an iLok Cloud session (set -Cloud:$false if a physical iLok is attached)
    [switch] $Install            # also copy the signed bundle into the Pro Tools plug-ins folder
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $Bundle)) { throw "Bundle not found: $Bundle" }
$dll = Join-Path $Bundle "Contents\x64\ToneFill.aaxplugin"
if (-not (Test-Path $dll)) { throw "Expected DLL not found at $dll - is the bundle structured as ToneFill.aaxplugin\Contents\x64\ToneFill.aaxplugin ?" }

# --- Locate wraptool + iloktool -------------------------------------------------
$roots = @("C:\Program Files\PACEAntiPiracy","C:\Program Files (x86)\PACEAntiPiracy",
           "C:\Program Files\iLok License Manager","C:\Program Files (x86)\iLok License Manager",
           "C:\Program Files","C:\Program Files (x86)")
$wt = $null; $it = $null
foreach ($r in $roots) {
    if (-not (Test-Path $r)) { continue }
    if (-not $wt) { $wt = Get-ChildItem $r -Recurse -Force -Filter "wraptool.exe" -ErrorAction SilentlyContinue | Select-Object -First 1 }
    if (-not $it) { $it = Get-ChildItem $r -Recurse -Force -Filter "iloktool.exe" -ErrorAction SilentlyContinue | Select-Object -First 1 }
    if ($wt -and $it) { break }
}
if (-not $wt) { throw "wraptool.exe not found - install the PACE Eden SDK (see prerequisites)." }
Write-Host "wraptool: $($wt.FullName)"
Write-Host "iloktool: $(if ($it) { $it.FullName } else { '(not found - install iLok License Manager if using -Cloud)' })"

# --- Self-signed code-signing cert (satisfies wraptool's Authenticode step) ------
$certPath = Join-Path $env:TEMP "tonefill-selfsign.pfx"; $certPwd = "selfsign-tonefill"
$sc = New-SelfSignedCertificate -Type CodeSigningCert -Subject "CN=cactuzz sound" `
        -CertStoreLocation "Cert:\CurrentUser\My" -KeyExportPolicy Exportable -KeySpec Signature `
        -KeyUsage DigitalSignature -NotAfter (Get-Date).AddYears(5)
$sp = ConvertTo-SecureString $certPwd -AsPlainText -Force
Export-PfxCertificate -Cert "Cert:\CurrentUser\My\$($sc.Thumbprint)" -FilePath $certPath -Password $sp | Out-Null
Write-Host "Self-signed cert thumbprint: $($sc.Thumbprint)"

# --- Open an iLok Cloud session (no physical iLok needed) -------------------------
if ($Cloud) {
    if (-not $it) { throw "-Cloud requested but iloktool.exe not found (install iLok License Manager)." }
    Write-Host ">>> Opening iLok Cloud session (account $Account)..."
    & $it.FullName cloud --open --account $Account --password $PacePassword -v
    if ($LASTEXITCODE -ne 0) { throw "iloktool cloud --open failed ($LASTEXITCODE)" }
}

try {
    # --- Sign (PACE wrap via the license + Authenticode via the self-signed cert) --
    Write-Host ">>> wraptool sign..."
    & $wt.FullName sign --verbose --account $Account --password $PacePassword `
        --signid $sc.Thumbprint --wcguid $Wcguid `
        --in $Bundle --out $Bundle
    if ($LASTEXITCODE -ne 0) { throw "wraptool sign failed ($LASTEXITCODE)" }

    Write-Host ">>> wraptool verify..."
    & $wt.FullName verify --verbose --in $Bundle
}
finally {
    if ($Cloud -and $it) { & $it.FullName cloud --close | Out-Null }
}

Write-Host "`nSIGNED: $Bundle" -ForegroundColor Green

# --- Optional install into the Pro Tools plug-ins folder --------------------------
if ($Install) {
    $dest = "C:\Program Files\Common Files\Avid\Audio\Plug-Ins\ToneFill.aaxplugin"
    if (Test-Path $dest) { Remove-Item $dest -Recurse -Force }
    Copy-Item $Bundle $dest -Recurse -Force
    Write-Host "Installed -> $dest" -ForegroundColor Green
}
