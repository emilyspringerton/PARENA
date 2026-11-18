# scripts/install-collection.ps1 -- install the Collection of Value addons and/or the SHANKPIT client (Windows).
#
# Two parts. Each runs when its folder is set, and at least one must be:
#
#   WoW addons -> WOW_ADDONS_DIR
#     Every Collection addon (Self Found, Fafnir's Bed, the hub, the games ...) ships in one zip,
#     CollectionOfValue.zip, attached to each green release of PARENA's main. Every folder in its AddOns/ is copied
#     into your client's Interface/AddOns. Each Collection addon folder is replaced; any other addon is left alone.
#
#   SHANKPIT client -> SHANKPIT_DIR
#     The Windows client from SHANKPIT's latest release (ShankPit_Client_build_<n>_<sha>.zip), unpacked into
#     SHANKPIT_DIR\ShankPit-<release tag>. An earlier release's folder is left where it is.
#
# Environment (or the matching parameter). No folder has a default: only you know where things go.
#   WOW_ADDONS_DIR      your WoW client's Interface\AddOns folder, e.g. <WoW>\_forever_\Interface\AddOns
#   COLLECTION_ZIP      optional: a CollectionOfValue zip already on disk, used instead of downloading one
#   COLLECTION_RELEASE  optional: a PARENA release tag such as v0.397.0 (default: the latest release)
#   COLLECTION_REPO     optional: owner/name (default emilyspringerton/PARENA)
#   SHANKPIT_DIR        the folder SHANKPIT releases are unpacked into, e.g. D:\Games\SHANKPIT
#   SHANKPIT_ZIP        optional: a ShankPit_Client zip already on disk
#   SHANKPIT_RELEASE    optional: a SHANKPIT release tag such as v0.298.0 (default: the latest release)
#   SHANKPIT_REPO       optional: owner/name (default emilyspringerton/SHANKPIT)
#
# usage (PowerShell):
#   $env:WOW_ADDONS_DIR = 'D:\World of Warcraft\_forever_\Interface\AddOns'
#   $env:SHANKPIT_DIR   = 'D:\Games\SHANKPIT'
#   powershell -ExecutionPolicy Bypass -File install-collection.ps1
param(
  [string]$AddOnsDir       = $env:WOW_ADDONS_DIR,
  [string]$Zip             = $env:COLLECTION_ZIP,
  [string]$Release         = $env:COLLECTION_RELEASE,
  [string]$Repo            = $env:COLLECTION_REPO,
  [string]$ShankpitDir     = $env:SHANKPIT_DIR,
  [string]$ShankpitZip     = $env:SHANKPIT_ZIP,
  [string]$ShankpitRelease = $env:SHANKPIT_RELEASE,
  [string]$ShankpitRepo    = $env:SHANKPIT_REPO
)
$ErrorActionPreference = 'Stop'
# Windows PowerShell 5.1 draws a progress bar per chunk, which makes Invoke-WebRequest crawl; turn it off.
$ProgressPreference = 'SilentlyContinue'
[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
# .NET's ZipFile, not Expand-Archive: the cmdlet stalls in non-interactive Windows PowerShell 5.1 hosts.
Add-Type -AssemblyName System.IO.Compression.FileSystem

function Die([string]$msg) { [Console]::Error.WriteLine("install-collection: $msg"); exit 2 }

function Check-Repo([string]$repo, [string]$what) {
  if ($repo -notmatch '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$') { Die "$what must be owner/name, got '$repo'" }
}
function Check-Tag([string]$tag, [string]$what) {
  if ($tag -and $tag -notmatch '^v[0-9]+\.[0-9]+\.[0-9]+$') { Die "$what must look like v1.2.3, got '$tag'" }
}
function Fetch([string]$url, [string]$out, [string]$hint) {
  Write-Host "install-collection: downloading $url"
  try { Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $out }
  catch { Die "download failed ($url): $($_.Exception.Message). $hint" }
}

if (-not $AddOnsDir -and -not $ShankpitDir) {
  Die 'nothing to install: set WOW_ADDONS_DIR (the WoW addons), SHANKPIT_DIR (the SHANKPIT client), or both.'
}
if ($AddOnsDir -and -not (Test-Path -LiteralPath $AddOnsDir -PathType Container)) { Die "no such folder: $AddOnsDir" }
if ($ShankpitDir -and -not (Test-Path -LiteralPath $ShankpitDir -PathType Container)) { Die "no such folder: $ShankpitDir" }
if (-not $Repo) { $Repo = 'emilyspringerton/PARENA' }
if (-not $ShankpitRepo) { $ShankpitRepo = 'emilyspringerton/SHANKPIT' }
Check-Repo $Repo 'COLLECTION_REPO'; Check-Repo $ShankpitRepo 'SHANKPIT_REPO'
Check-Tag $Release 'COLLECTION_RELEASE'; Check-Tag $ShankpitRelease 'SHANKPIT_RELEASE'

$work = Join-Path ([IO.Path]::GetTempPath()) ("collection-install-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
try {
  # ---- the WoW addons ---------------------------------------------------------------------------------------------
  if ($AddOnsDir) {
    if ($Zip) {
      if (-not (Test-Path -LiteralPath $Zip -PathType Leaf)) { Die "no such zip: $Zip" }
      $zipPath = $Zip
    } else {
      if ($Release) { $url = "https://github.com/$Repo/releases/download/$Release/CollectionOfValue.zip" }
      else          { $url = "https://github.com/$Repo/releases/latest/download/CollectionOfValue.zip" }
      $zipPath = Join-Path $work 'CollectionOfValue.zip'
      Fetch $url $zipPath 'If the release has no CollectionOfValue.zip yet, the repository variable WOW_INTERFACE is not set (docs/COLLECTION_SHARING.md).'
    }
    $unz = Join-Path $work 'client'
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
    Write-Host '  Restart WoW or /reload, then /collection (every addon) or /sf (Self Found) in game.'
  }

  # ---- the SHANKPIT client ----------------------------------------------------------------------------------------
  if ($ShankpitDir) {
    $tag = $ShankpitRelease
    if ($ShankpitZip) {
      if (-not (Test-Path -LiteralPath $ShankpitZip -PathType Leaf)) { Die "no such zip: $ShankpitZip" }
      $spZip = $ShankpitZip
      if (-not $tag) { $tag = [IO.Path]::GetFileNameWithoutExtension($ShankpitZip) }
    } else {
      if ($tag) { $api = "https://api.github.com/repos/$ShankpitRepo/releases/tags/$tag" }
      else      { $api = "https://api.github.com/repos/$ShankpitRepo/releases/latest" }
      try { $rel = Invoke-RestMethod -UseBasicParsing -Uri $api -Headers @{ 'Accept' = 'application/vnd.github+json' } }
      catch { Die "could not read the SHANKPIT release ($api): $($_.Exception.Message)" }
      $tag = $rel.tag_name
      $asset = $rel.assets | Where-Object { $_.name -match '^ShankPit_Client_build_[0-9]+_[0-9a-f]+\.zip$' } | Select-Object -First 1
      if (-not $asset) { Die "release $tag of $ShankpitRepo has no ShankPit_Client_build_<n>_<sha>.zip" }
      $spZip = Join-Path $work $asset.name
      Fetch $asset.browser_download_url $spZip 'Check your connection and try again.'
    }
    if ($tag -notmatch '^[A-Za-z0-9._-]+$') { Die "unexpected release name '$tag'" }
    $dest = Join-Path $ShankpitDir ("ShankPit-" + $tag)
    if (Test-Path -LiteralPath $dest) { Remove-Item -LiteralPath $dest -Recurse -Force }
    [IO.Compression.ZipFile]::ExtractToDirectory($spZip, $dest)
    $exe = Get-ChildItem -LiteralPath $dest -Recurse -Filter 'shank_lobby*.exe' | Select-Object -First 1
    Write-Host "install-collection: SHANKPIT $tag unpacked to $dest"
    if ($exe) { Write-Host "  Start it with: $($exe.FullName)" }
    else      { Write-Host '  No shank_lobby*.exe was found in it; see the README inside that folder.' }
  }
} finally {
  Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}
