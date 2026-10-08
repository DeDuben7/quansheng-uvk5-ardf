<#
.SYNOPSIS
  Renders porto-front.svg (the drawn UV-K5 front view) to a 310x645 PNG.

.DESCRIPTION
  Headless Edge screenshots the SVG at 2x (620x1290) on a transparent
  background via a temporary wrapper page next to the SVG; Python + Pillow
  then crops away the bottom of the (slightly taller) window and scales the
  result down to 310x645 RGBA with LANCZOS. The 310x645 size and coordinate
  system match the manual's callout overlay (handleiding.html) and the
  flasher wizard.

  Edge runs with a throwaway profile in a fresh, GUID-named folder under the
  system temp folder; that folder is the only thing the script deletes. Edge
  is stopped if it takes longer than 60 seconds.

.PARAMETER Out
  File name (in this folder) or path of the PNG. Default: porto-front.png,
  the picture the manual and the flasher wizard use.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File docs\manual\img\render.ps1
#>
param(
    [string]$Out = 'porto-front.png'
)

$ErrorActionPreference = 'Stop'

$edge = @(
    "${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe",
    "$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $edge) { throw 'Microsoft Edge (msedge.exe) not found' }

$python = Get-Command python -ErrorAction SilentlyContinue
if (-not $python) { throw 'python not found (needs Python 3 with Pillow)' }

$svg = Join-Path $PSScriptRoot 'porto-front.svg'
if (-not (Test-Path $svg)) { throw "SVG not found: $svg" }
if ([IO.Path]::IsPathRooted($Out)) { $png = $Out } else { $png = Join-Path $PSScriptRoot $Out }

# The wrapper sits next to the SVG so the relative <img src> resolves.
$wrapper = Join-Path $PSScriptRoot '_render.html'
$html = '<!doctype html><html><body style="margin:0;background:transparent">' +
        '<img src="porto-front.svg" width="620" height="1290" style="display:block"></body></html>'
[IO.File]::WriteAllText($wrapper, $html, (New-Object Text.UTF8Encoding $false))
$uri = ([Uri](Resolve-Path $wrapper).Path).AbsoluteUri

# A fresh GUID-named work folder holds the throwaway Edge profile (it keeps the
# user's own Edge and any open windows out of it) and the screenshot.
$work = Join-Path ([IO.Path]::GetTempPath()) ('porto-render-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
$profileDir = Join-Path $work 'profile'
$shot = Join-Path $work 'shot.png'

# The paths are quoted: a %TEMP% or checkout folder with a space must still work.
$edgeArgs = @(
    '--headless=new', '--disable-gpu', '--no-first-run', '--hide-scrollbars',
    "--user-data-dir=`"$profileDir`"", '--default-background-color=00000000',
    '--window-size=620,1400', "--screenshot=`"$shot`"", $uri
)

try {
    $p = Start-Process -FilePath $edge -ArgumentList $edgeArgs -PassThru -WindowStyle Hidden
    $null = $p.Handle    # keeps the exit code readable after the process ends
    if (-not $p.WaitForExit(60000)) {
        & taskkill.exe /F /T /PID $p.Id | Out-Null    # only the Edge started here, and its children
        throw 'Edge did not finish within 60 seconds'
    }
    if (-not (Test-Path $shot)) { throw "Edge made no screenshot (exit code $($p.ExitCode))" }

    # Crop to the 620x1290 image and scale down to 310x645 (RGBA, transparent outside the radio).
    $py = 'import sys; from PIL import Image; ' +
          'im = Image.open(sys.argv[1]).convert(''RGBA''); ' +
          'assert im.width >= 620 and im.height >= 1290, im.size; ' +
          'im.crop((0, 0, 620, 1290)).resize((310, 645), Image.LANCZOS).save(sys.argv[2])'
    & $python.Source -c $py $shot $png
    if ($LASTEXITCODE -ne 0) { throw "Pillow crop/resize failed (exit code $LASTEXITCODE)" }
} finally {
    Remove-Item $wrapper -ErrorAction SilentlyContinue
    # Only ever remove the GUID-named folder created above, never the temp folder itself.
    if ($work -and ((Split-Path $work -Leaf) -like 'porto-render-*') -and (Test-Path $work)) {
        Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
    }
}

Write-Host "porto-front.svg -> $png (310x645)"
