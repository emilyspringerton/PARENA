# scripts/install-collection.ps1 -- install the Collection of Value addons from a PARENA release (Windows).
#
# Every Collection addon ships in one zip, CollectionOfValue.zip, attached to each green release of PARENA's main
# (.github/workflows/ci.yml, "Tag and release"). This script copies every folder in its AddOns/ into your client's
# Interface/AddOns folder. Each addon folder it installs is replaced; it leaves any other addon alone.
#
# Environment (or the matching parameter):
#   WOW_ADDONS_DIR      REQUIRED. Your client's Interface/AddOns folder, e.g. <WoW>\_forever_\Interface\AddOns.
#                       There is no default, because only you know where your client is installed.
#   COLLECTION_ZIP      Optional. A CollectionOfValue zip already on disk, used instead of downloading one.
#   COLLECTION_RELEASE  Optional. A release tag such as v0.397.0. The default is the latest release.
#   COLLECTION_REPO     Optional. owner/name of the repository (default emilyspringerton/PARENA).
#
# usage (PowerShell):
#   $env:WOW_ADDONS_DIR = 'D:\World of Warcraft\_forever_\Interface\AddOns'
#   powershell -ExecutionPolicy Bypass -File install-collection.ps1
param(
  [string]$AddOnsDir = $env:WOW_ADDONS_DIR,
  [string]$Zip       = $env:COLLECTION_ZIP,
  [string]$Release   = $env:COLLECTION_RELEASE,
  [string]$Repo      = $env:COLLECTION_REPO
)
$ErrorActionPreference = 'Stop'
# Windows PowerShell 5.1 draws a progress bar per chunk, which makes Invoke-WebRequest crawl; turn it off.
$ProgressPreference = 'SilentlyContinue'

function Die([string]$msg) { [Console]::Error.WriteLine("install-collection: $msg"); exit 2 }

if (-not $AddOnsDir) { Die 'WOW_ADDONS_DIR is not set. Point it at your client''s Interface\AddOns folder.' }
if (-not (Test-Path -LiteralPath $AddOnsDir -PathType Container)) { Die "no such folder: $AddOnsDir" }
if (-not $Repo) { $Repo = 'emilyspringerton/PARENA' }
if ($Repo -notmatch '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$') { Die "COLLECTION_REPO must be owner/name, got '$Repo'" }
if ($Release -and $Release -notmatch '^v[0-9]+\.[0-9]+\.[0-9]+$') { Die "COLLECTION_RELEASE must look like v1.2.3, got '$Release'" }

$work = Join-Path ([IO.Path]::GetTempPath()) ("collection-install-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
try {
  if ($Zip) {
    if (-not (Test-Path -LiteralPath $Zip -PathType Leaf)) { Die "no such zip: $Zip" }
    $zipPath = $Zip
  } else {
    if ($Release) { $url = "https://github.com/$Repo/releases/download/$Release/CollectionOfValue.zip" }
    else          { $url = "https://github.com/$Repo/releases/latest/download/CollectionOfValue.zip" }
    $zipPath = Join-Path $work 'CollectionOfValue.zip'
    [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
    Write-Host "install-collection: downloading $url"
    try { Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $zipPath }
    catch { Die "download failed ($url): $($_.Exception.Message). If the release has no CollectionOfValue.zip yet, the repository variable WOW_INTERFACE is not set (docs/COLLECTION_SHARING.md)." }
  }

  $unz = Join-Path $work 'client'
  # .NET's ZipFile, not Expand-Archive: the cmdlet stalls in non-interactive Windows PowerShell 5.1 hosts.
  Add-Type -AssemblyName System.IO.Compression.FileSystem
  [IO.Compression.ZipFile]::ExtractToDirectory($zipPath, $unz)
  $src = Join-Path $unz 'AddOns'
  if (-not (Test-Path -LiteralPath (Join-Path $src 'RefluxCore') -PathType Container)) {
    Die 'that zip has no AddOns\RefluxCore; it is not a Collection of Value client'
  }

  $names = @()
  foreach ($d in Get-ChildItem -LiteralPath $src -Directory) {
    $dest = Join-Path $AddOnsDir $d.Name
    if (Test-Path -LiteralPath $dest) { Remove-Item -LiteralPath $dest -Recurse -Force }
    Copy-Item -LiteralPath $d.FullName -Destination $dest -Recurse
    $names += $d.Name
  }
  $ver = ''
  $vf = Join-Path $unz 'VERSION'
  if (Test-Path -LiteralPath $vf) { $ver = (Get-Content -LiteralPath $vf -TotalCount 1) }
  Write-Host "install-collection: installed $($names.Count) addons ($ver) to $AddOnsDir"
  Write-Host ('  ' + ($names -join ', '))
  Write-Host 'Restart the client or /reload, then /collection in game.'
} finally {
  Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}
