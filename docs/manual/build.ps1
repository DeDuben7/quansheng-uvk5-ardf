<#
.SYNOPSIS
  Renders the Dutch manual (handleiding.html) to PDF with headless Edge.

.DESCRIPTION
  Reads SCOUT_VERSION from core/Inc/scout/version.h and the short commit from
  `git describe --always --dirty --abbrev=7 --exclude '*'` (the exclude keeps
  tags such as v1.0 out of it), fills in the {{VERSION}} and {{COMMIT}} tokens
  into a temporary _build.html next to the source (so the relative image
  paths keep working), and prints it to Handleiding-Scout-porto-<version>.pdf.

  Edge runs with a throwaway profile in a fresh, GUID-named folder under the
  system temp folder; that folder is the only thing the script deletes besides
  its own _build.html and the PDF it is about to rewrite. Edge is stopped if
  it takes longer than 60 seconds.

.PARAMETER OutDir
  Where to write the PDF. Default: this folder.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File docs\manual\build.ps1 -OutDir release\1.0
#>
param(
    [string]$OutDir = $PSScriptRoot
)

$ErrorActionPreference = 'Stop'

$repo    = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$header  = Join-Path $repo 'core\Inc\scout\version.h'
$match   = Select-String -Path $header -Pattern '#define\s+SCOUT_VERSION\s+"([^"]+)"'
if (-not $match) { throw "SCOUT_VERSION not found in $header" }
$version = $match.Matches[0].Groups[1].Value

# Short commit of the source this manual belongs to ("-dirty" if not committed).
# --exclude '*': without it an old tag (v1.0) would show up as "v1.0-3-g1234567".
try {
    $commit = (& git -C $repo describe --always --dirty --abbrev=7 --exclude '*')
    if ($LASTEXITCODE -ne 0) { $commit = $null }
} catch {
    $commit = $null          # no git, or not a git checkout
}
$commit = if ($commit) { "$commit".Trim() } else { 'onbekend' }

$edge = @(
    "${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe",
    "$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $edge) { throw 'Microsoft Edge (msedge.exe) not found' }

$source = Join-Path $PSScriptRoot 'handleiding.html'
$temp   = Join-Path $PSScriptRoot '_build.html'
$html   = [IO.File]::ReadAllText($source, [Text.Encoding]::UTF8).Replace('{{VERSION}}', $version).Replace('{{COMMIT}}', $commit)
[IO.File]::WriteAllText($temp, $html, (New-Object Text.UTF8Encoding $false))

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$pdf = Join-Path (Resolve-Path $OutDir) "Handleiding-Scout-porto-$version.pdf"
if (Test-Path $pdf) { Remove-Item $pdf }

# A throwaway profile keeps the user's Edge (and any open windows) out of it.
# It lives in a fresh GUID-named folder, the only folder removed afterwards.
$work = Join-Path ([IO.Path]::GetTempPath()) ('scout-manual-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
$profileDir = Join-Path $work 'profile'
$uri = ([Uri](Resolve-Path $temp).Path).AbsoluteUri
# The paths are quoted: a %TEMP% or output folder with a space must still work.
$edgeArgs = @(
    '--headless=new', '--disable-gpu', '--no-first-run', '--no-pdf-header-footer',
    "--user-data-dir=`"$profileDir`"", '--virtual-time-budget=5000',
    "--print-to-pdf=`"$pdf`"", $uri
)
try {
    $p = Start-Process -FilePath $edge -ArgumentList $edgeArgs -PassThru -WindowStyle Hidden
    $null = $p.Handle    # keeps the exit code readable after the process ends
    if (-not $p.WaitForExit(60000)) {
        & taskkill.exe /F /T /PID $p.Id | Out-Null    # only the Edge started here, and its children
        throw 'Edge did not finish within 60 seconds'
    }
} finally {
    Remove-Item $temp -ErrorAction SilentlyContinue
    # Only ever remove the GUID-named folder created above, never the temp folder itself.
    if ($work -and ((Split-Path $work -Leaf) -like 'scout-manual-*') -and (Test-Path $work)) {
        Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
    }
}

if (-not (Test-Path $pdf) -or (Get-Item $pdf).Length -lt 50KB) {
    throw "PDF not created (Edge exit code $($p.ExitCode)): $pdf"
}
Write-Host "Manual v$version ($commit) -> $pdf ($([math]::Round((Get-Item $pdf).Length / 1KB)) KB)"
