<#
    One-shot: download the CI-built unsigned Windows ToneFill AAX, assemble the bundle, PACE-sign it
    (iLok Cloud + self-signed cert, no paid cert / no physical iLok), and install it for Pro Tools.

    Run in a Windows VM that has: iLok License Manager (logged in once as 'cactuzz') + PACE Eden SDK
    (wraptool) + GitHub CLI (gh, authenticated). Prompts securely for the iLok account password.

    Usage (one line):
      powershell -ExecutionPolicy Bypass -File C:\ToneFill\bootstrap.ps1
#>

$ErrorActionPreference = 'Stop'
$Account = 'cactuzz'
$Wcguid  = '53B16D60-73F3-11F1-B005-005056920FF7'
$RunId   = '31094592635'   # CI run holding the unsigned artifact
$Work    = 'C:\ToneFill'
$Bundle  = Join-Path $Work 'ToneFill.aaxplugin'

function Step($m) { Write-Host "`n==> $m" -ForegroundColor Cyan }

New-Item -ItemType Directory -Force -Path $Work | Out-Null
Set-Location $Work

# --- 0. Tools present? ----------------------------------------------------------
Step "Locating wraptool + iloktool + gh"
$roots = @("C:\Program Files (x86)\PACEAntiPiracy","C:\Program Files\PACEAntiPiracy",
           "C:\Program Files (x86)\iLok License Manager","C:\Program Files\iLok License Manager",
           "C:\Program Files","C:\Program Files (x86)")
$wt=$null; $it=$null
foreach ($r in $roots) {
    if (-not (Test-Path $r)) { continue }
    if (-not $wt) { $wt = Get-ChildItem $r -Recurse -Force -Filter wraptool.exe -ErrorAction SilentlyContinue | Select-Object -First 1 }
    if (-not $it) { $it = Get-ChildItem $r -Recurse -Force -Filter iloktool.exe -ErrorAction SilentlyContinue | Select-Object -First 1 }
    if ($wt -and $it) { break }
}
if (-not $wt) { throw "wraptool.exe not found - install the PACE Eden SDK." }
if (-not $it) { throw "iloktool.exe not found - install iLok License Manager." }
if (-not (Get-Command gh -ErrorAction SilentlyContinue)) { throw "gh (GitHub CLI) not found / not on PATH." }
Write-Host "wraptool: $($wt.FullName)"
Write-Host "iloktool: $($it.FullName)"
$signtool = Get-ChildItem "C:\Program Files (x86)\Windows Kits" -Recurse -Filter signtool.exe -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match '\\x64\\' } | Sort-Object FullName -Descending | Select-Object -First 1
Write-Host "signtool: $(if ($signtool) { $signtool.FullName } else { '(not found - will let wraptool locate it)' })"

# --- 1. Download the unsigned bundle from CI ------------------------------------
Step "Downloading unsigned AAX from CI run $RunId"
if (Test-Path "$Work\unsigned") { Remove-Item "$Work\unsigned" -Recurse -Force }
gh run download $RunId --repo cactuzzsound/tonefill-ara -n ToneFill-Windows-AAX-unsigned -D "$Work\unsigned"
$srcDll = Join-Path $Work 'unsigned\Contents\x64\ToneFill.aaxplugin'
if (-not (Test-Path $srcDll)) { throw "unsigned DLL not found at $srcDll" }

# --- 2. Assemble the folder bundle Pro Tools/wraptool expect --------------------
Step "Assembling bundle: $Bundle"
if (Test-Path $Bundle) { Remove-Item $Bundle -Recurse -Force }
$x64 = Join-Path $Bundle 'Contents\x64'
New-Item -ItemType Directory -Force -Path $x64 | Out-Null
Copy-Item $srcDll (Join-Path $x64 'ToneFill.aaxplugin') -Force

# --- 3. Self-signed code-signing cert (satisfies wraptool's Authenticode step) --
Step "Creating a self-signed code-signing certificate"
$sc = New-SelfSignedCertificate -Type CodeSigningCert -Subject "CN=cactuzz sound" `
        -CertStoreLocation "Cert:\CurrentUser\My" -KeyExportPolicy Exportable -KeySpec Signature `
        -KeyUsage DigitalSignature -NotAfter (Get-Date).AddYears(5)
Write-Host "thumbprint: $($sc.Thumbprint)"

# --- 4. iLok password (typed securely, never echoed) ----------------------------
$secure = Read-Host "Enter the iLok account ($Account) password" -AsSecureString
$bstr   = [System.Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure)
$Pace   = [System.Runtime.InteropServices.Marshal]::PtrToStringAuto($bstr)
[System.Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr)

# --- 5. Open iLok Cloud session -------------------------------------------------
Step "Opening iLok Cloud session"
& $it.FullName cloud --open --account $Account --password $Pace -v
if ($LASTEXITCODE -ne 0) { throw "iloktool cloud --open failed ($LASTEXITCODE) - check the password / iLok LM login." }

try {
    # --- 6. Sign -----------------------------------------------------------------
    Step "Signing with wraptool"
    # Local signing using the credentials in the open iLok Cloud session: the Eden Tools license and
    # the publisher signing certificate were deposited to "cactuzz's Cloud", so wraptool finds them
    # via the session. (No --allowsigningservice: that routes to PACE's server-side signing service,
    # which this publisher isn't enrolled in.)
    $args = @('sign','--verbose','--account',$Account,'--password',$Pace,
              '--signid',$sc.Thumbprint,'--wcguid',$Wcguid,
              '--in',$Bundle,'--out',$Bundle)
    if ($signtool) { $args += @('--signtool',$signtool.FullName) }
    & $wt.FullName @args
    if ($LASTEXITCODE -ne 0) { throw "wraptool sign failed ($LASTEXITCODE)" }

    Step "Verifying"
    & $wt.FullName verify --verbose --in $Bundle
}
finally {
    & $it.FullName cloud --close | Out-Null
}

# --- 7. Install for Pro Tools ---------------------------------------------------
Step "Installing to the Pro Tools plug-ins folder"
$dest = "C:\Program Files\Common Files\Avid\Audio\Plug-Ins\ToneFill.aaxplugin"
if (Test-Path $dest) { Remove-Item $dest -Recurse -Force }
Copy-Item $Bundle $dest -Recurse -Force

Write-Host "`nDONE - signed + installed:" -ForegroundColor Green
Write-Host "  $dest"
