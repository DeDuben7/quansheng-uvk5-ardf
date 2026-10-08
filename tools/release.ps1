<#
.SYNOPSIS
  Builds a scout release (the four GitHub release assets) and, with -Publish,
  publishes it on GitHub.

.DESCRIPTION
  1. Checks, before anything is built or deleted: the -Version format;
     SCOUT_VERSION (core/Inc/scout/version.h) equals -Version without its
     suffix; a clean working tree (the commit is baked into the firmware and
     the EXE); FLASHER_VERSION; Python; with -Publish also the publish
     preconditions listed below.
  2. Exports the Prolific PL2303 driver 3.3.11.152 from this PC
     (pnputil /export-driver) into tools/scout-flasher/driver/pl2303-3.3.11.152
     (gitignored: only the EXE contains it) and checks its signature.
  3. Clean-builds V1 and V3 firmware.
  4. Checks the SCOUT-VERSION:<v>+<commit> marker in both raw images, and that
     the packed V1 image is the raw one packed again.
  5. Runs the emulator tests and checks the manual screens are current.
  6. Renders the manual PDF (into release/_work-<v>/, removed at the end).
  7. Runs the flasher unit tests.
  8. Writes scout-firmware.zip, Handleiding-Scout-porto.pdf and
     scout-update.json (tools/make_release_assets.py).
  9. Builds ScoutFlasher.exe with PyInstaller, carrying the commit
     (sf/_build_info.py) and a Windows version resource
     (build/version_info.txt), and adds its SHA-256 to scout-update.json.
  Output: release/<v>/ with exactly ScoutFlasher.exe, scout-firmware.zip,
  scout-update.json and Handleiding-Scout-porto.pdf.

  With -Publish, after a typed "yes" (or -Yes):
  10. tags v<v> on HEAD (an existing tag is kept only if it is on HEAD);
  11. pushes HEAD to origin/main, then the tag;
  12. creates the GitHub release v<v> with the four assets. Its notes are the
      "## <v>" section of CHANGELOG.md, or "## <v without suffix>"; they are
      written to release/notes-<v>.md.
  Publish preconditions, checked before the build and again before tagging:
  git config user.email is a GitHub noreply address (@users.noreply.github.com;
  the commit and the tag are public), the GitHub CLI (gh) on PATH, a clean
  tree, branch main, origin is exactly DeDuben7/quansheng-uvk5-ardf, gh auth
  status OK, push rights (git push --dry-run origin HEAD:main), release notes
  present.
  Every git command that talks to origin uses the GitHub CLI's login for that
  one command (-c credential.helper=!gh auth git-credential), never Git
  Credential Manager, and never asks on the console or in a window. No git
  config is changed. A failed publish can be run again with the same command.

  Needs: Git for Windows (bash), arm-none-eabi-gcc + make on PATH,
  w64devkit (C:\w64devkit), 64-bit Python 3.10+ with pyserial, crcmod (V1
  packer), certifi (its CA bundle goes into the EXE for the HTTPS downloads),
  pytest and PyInstaller, Microsoft Edge, and the Prolific driver
  3.3.11.152 installed on this PC. -Publish also needs the GitHub CLI.

.PARAMETER Version
  X.Y or X.Y.Z, optionally with a pre-release suffix such as 1.1-rc1. The
  version.h check and the firmware marker use the part before "-"; the tag,
  the release title and scout-update.json use the full string.

.PARAMETER MinFlasher
  The oldest ScoutFlasher that may use this release (min_flasher in
  scout-update.json). Raise it only when the package format or the flashing
  changes. Default 1.1.

.PARAMETER Publish
  Tag, push and create the GitHub release (public!) after the build.

.PARAMETER Prerelease
  With -Publish: mark the GitHub release as a pre-release. It never becomes
  "Latest", so ScoutFlasher does not see it. Required for a -suffix version.

.PARAMETER Yes
  With -Publish: skip the typed confirmation (non-interactive runs).

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File tools\release.ps1 -Version 1.1

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File tools\release.ps1 -Version 1.1-rc1 -Publish -Prerelease
#>
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [string]$MinFlasher = '1.1',
    [switch]$Publish,
    [switch]$Prerelease,
    [switch]$Yes
)

$ErrorActionPreference = 'Stop'

# The version names folders, the tag and the release: check it before anything else.
if ($Version -cnotmatch '^[0-9]+\.[0-9]+(\.[0-9]+)?(-[0-9A-Za-z]+(\.[0-9A-Za-z]+)*)?\z') {
    throw "-Version '$Version' is not X.Y, X.Y.Z or X.Y-suffix (for example 1.1 or 1.1-rc1)"
}
if ($MinFlasher -cnotmatch '^[0-9]+(\.[0-9]+)*(-[0-9A-Za-z]+(\.[0-9A-Za-z]+)*)?\z') {
    throw "-MinFlasher '$MinFlasher' is not a version"
}
if ($Publish -and $Version.Contains('-') -and -not $Prerelease) {
    throw "v$Version has a pre-release suffix: publish it with -Prerelease (it must never become Latest)"
}
if (-not $Publish -and ($Prerelease -or $Yes)) { Write-Warning '-Prerelease and -Yes only apply with -Publish' }

$baseVersion = $Version.Split('-')[0]
$repo      = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$out       = Join-Path $repo "release\$Version"
$work      = Join-Path $repo "release\_work-$Version"
$notesFile = Join-Path $repo "release\notes-$Version.md"
$flasher   = Join-Path $repo 'tools\scout-flasher'
$driverDir = Join-Path $flasher 'driver\pl2303-3.3.11.152'
$v1dir     = Join-Path $repo 'platform\uv-k5-v1-dp32g030'
$v3dir     = Join-Path $repo 'platform\uv-k5-v3-py32f071'
$ghRepo    = 'DeDuben7/quansheng-uvk5-ardf'
$assetNames = @('ScoutFlasher.exe', 'scout-firmware.zip', 'scout-update.json', 'Handleiding-Scout-porto.pdf')
$utf8      = New-Object Text.UTF8Encoding $false

function Step($text) { Write-Host "`n== $text" -ForegroundColor Cyan }

# Every recursive delete goes through here: an absolute path strictly inside
# the repo (never the repo itself or anything above it). .NET never recurses
# through a junction or symlink: it removes the link (and reports an error for
# a junction), never the files it points to.
function Remove-InRepo([string]$Path) {
    if (-not [IO.Path]::IsPathRooted($Path)) { throw "Refusing to delete '$Path': not an absolute path" }
    $full = [IO.Path]::GetFullPath($Path).TrimEnd('\')
    $root = [IO.Path]::GetFullPath($repo).TrimEnd('\')
    if (-not $full.StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to delete '$full': not inside the repo $root"
    }
    if ([IO.Directory]::Exists($full)) {
        [IO.Directory]::Delete($full, $true)
    } elseif ([IO.File]::Exists($full)) {
        throw "Refusing to delete '$full': a file where a folder was expected"
    }
}

# Same command as the platform Makefiles (-DSCOUT_GIT_HASH). --exclude '*'
# keeps tags (v1.0) out of it: "8b8060a", or "8b8060a-dirty".
function Get-Commit {
    $c = & git -C $repo describe --always --dirty --abbrev=7 --exclude '*'
    if ($LASTEXITCODE -ne 0 -or -not $c) { throw 'git describe failed' }
    return "$c".Trim()
}

function Assert-CleanTree {
    $c = Get-Commit
    if ($c -match '-dirty$') {
        throw "Working tree is dirty ($c): commit first, a release must come from a clean commit"
    }
    return $c
}

# The "## <v>" (else "## <base v>") section of CHANGELOG.md.
function Get-ReleaseNotes {
    $path = Join-Path $repo 'CHANGELOG.md'
    if (-not (Test-Path -LiteralPath $path)) { throw 'CHANGELOG.md not found' }
    $lines = [IO.File]::ReadAllLines($path, [Text.Encoding]::UTF8)
    foreach ($v in (@($Version, $baseVersion) | Select-Object -Unique)) {
        $heading = '^##\s+' + [regex]::Escape($v) + '(\s.*)?$'
        for ($i = 0; $i -lt $lines.Count; $i++) {
            if ($lines[$i] -notmatch $heading) { continue }
            $body = @()
            for ($j = $i + 1; $j -lt $lines.Count -and $lines[$j] -notmatch '^##\s'; $j++) { $body += $lines[$j] }
            $text = ($body -join "`n").Trim()
            if (-not $text) { throw "The '## $v' section of CHANGELOG.md is empty" }
            return $text
        }
    }
    throw "CHANGELOG.md has no '## $Version' or '## $baseVersion' section: write the release notes first"
}

# True only for the exact GitHub URL of $ghRepo: https://github.com/<repo>[.git][/]
# or git@github.com:<repo>[.git]. The host is case-insensitive, the rest is not.
function Test-OriginUrl([string]$url) {
    $repoRe = [regex]::Escape($ghRepo)
    return ($url -cmatch "^https://(?i:github\.com)/$repoRe(\.git)?/?\z") -or
           ($url -cmatch "^git@(?i:github\.com):$repoRe(\.git)?\z")
}

# Every git command that talks to origin goes through here. It logs in with the
# GitHub CLI (gh auth git-credential) for this one command only: the empty
# credential.helper first resets the helper list, so Git Credential Manager
# (system gitconfig) is never asked and cannot open a window or hang in a
# non-interactive run. Nothing is written to any git config. The output is
# shown, not returned, so callers can use it inside functions that return a value.
# Callers check $LASTEXITCODE.
function Invoke-GitRemote {
    $env:GIT_TERMINAL_PROMPT = '0'     # never ask for a password on the console
    $env:GCM_INTERACTIVE = 'never'     # nor in a Git Credential Manager window
    & git -C $repo -c credential.helper= -c 'credential.helper=!gh auth git-credential' @args | Out-Host
}

function Assert-PublishReady {
    # The commit and the tag are public, and they carry this address.
    $email = "$(& git -C $repo config user.email)".Trim()
    if (-not $email.EndsWith('@users.noreply.github.com', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Set a GitHub noreply address first: git config user.email <id>+<user>@users.noreply.github.com'
    }
    if (-not (Get-Command gh -CommandType Application -ErrorAction SilentlyContinue)) {
        throw 'Install the GitHub CLI: winget install GitHub.cli, then gh auth login'
    }
    Assert-CleanTree | Out-Null
    $branch = & git -C $repo symbolic-ref --quiet --short HEAD
    if ("$branch".Trim() -ne 'main') { throw 'Publish from the main branch' }
    if (@(& git -C $repo remote) -notcontains 'origin') {
        throw "No git remote 'origin': git remote add origin https://github.com/$ghRepo.git"
    }
    $url = "$(& git -C $repo remote get-url origin)".Trim()
    if (-not (Test-OriginUrl $url)) {
        throw "origin is $url, not https://github.com/$ghRepo.git (or git@github.com:$ghRepo.git)"
    }
    & gh auth status | Out-Host      # shown, not part of the return value
    if ($LASTEXITCODE -ne 0) { throw 'The GitHub CLI is not logged in: gh auth login' }
    # Can we push? Fails here, before the long build, when this login has no push
    # rights to origin (or origin/main has commits this branch lacks).
    Invoke-GitRemote push --dry-run origin HEAD:main
    if ($LASTEXITCODE -ne 0) {
        throw ("git push --dry-run origin HEAD:main failed (see above). For 'authentication failed' or " +
               "'permission denied': run gh auth login (an account that may push to $ghRepo), then run " +
               "this command again. For 'rejected': origin/main has commits this branch does not have.")
    }
    return Get-ReleaseNotes
}

function Show-Assets {
    foreach ($name in $assetNames) {
        $f = Get-Item -LiteralPath (Join-Path $out $name)
        $h = (Get-FileHash -LiteralPath $f.FullName -Algorithm SHA256).Hash.ToLower()
        Write-Host ("  {0,-28} {1,12:N0} bytes  {2}" -f $f.Name, $f.Length, $h)
    }
}

$bash = @("$env:ProgramFiles\Git\bin\bash.exe", "${env:ProgramFiles(x86)}\Git\bin\bash.exe") |
        Where-Object { Test-Path $_ } | Select-Object -First 1
$repoPosix = '/' + $repo.Substring(0, 1).ToLower() + $repo.Substring(2).Replace('\', '/')

function Invoke-Bash([string]$dir, [string]$cmd) {
    & $bash -c "cd '$repoPosix/$dir' && $cmd"
    if ($LASTEXITCODE -ne 0) { throw "failed in ${dir}: $cmd" }
}

# 1 ---------------------------------------------------------------------
Step "Checks for $Version"
if ($Publish) {
    $notes = Assert-PublishReady
    Write-Host "  ok  publish preconditions (noreply email, gh, clean tree, main, origin, gh auth, CHANGELOG)"
}
$header = Get-Content -LiteralPath (Join-Path $repo 'core\Inc\scout\version.h') -Raw
if ($header -notmatch '#define\s+SCOUT_VERSION\s+"([^"]+)"') { throw 'SCOUT_VERSION not found' }
if ($Matches[1] -ne $baseVersion) { throw "version.h says $($Matches[1]), not $baseVersion. Bump it first." }
$commit = Assert-CleanTree
$init = [IO.File]::ReadAllText((Join-Path $flasher 'sf\__init__.py'))
if ($init -notmatch '(?m)^FLASHER_VERSION\s*=\s*"([^"]+)"') { throw 'FLASHER_VERSION not found in sf/__init__.py' }
$flasherVersion = $Matches[1]
if ($flasherVersion -cnotmatch '^[0-9]+(\.[0-9]+){0,3}\z') { throw "FLASHER_VERSION '$flasherVersion' must be 1 to 4 numbers" }
if (-not $bash) { throw 'Git for Windows (bash.exe) not found' }
& python -c "import struct, sys, crcmod, serial, certifi, PyInstaller, pytest; sys.exit(0 if struct.calcsize('P') == 8 else 1)"
if ($LASTEXITCODE -ne 0) { throw 'Need 64-bit Python with crcmod, pyserial, certifi, PyInstaller and pytest' }
$date = Get-Date -Format 'yyyy-MM-dd'
Write-Host "  ok  release $Version, firmware $baseVersion, commit $commit, ScoutFlasher $flasherVersion, min_flasher $MinFlasher"

# 2 ---------------------------------------------------------------------
Step 'PL2303 driver 3.3.11.152 (exported from this PC)'
# pnputil prints one "Key: Value" block per driver package. Pick the original
# ser2pl.inf 3.3.11.152, never the 3.9.6.0 package (ser2pl_1.inf) that
# Windows Update installs.
$enum = & pnputil /enum-drivers
if ($LASTEXITCODE -ne 0) { throw "pnputil /enum-drivers failed (exit $LASTEXITCODE)" }
$blocks = @(); $block = @{}
foreach ($line in @($enum) + '') {
    if ($line -match '^\s*$') { if ($block.Count) { $blocks += $block; $block = @{} }; continue }
    if ($line -match '^\s*([^:]+?)\s*:\s*(.*?)\s*$') { $block[$Matches[1]] = $Matches[2] }
}
$pkg = $blocks | Where-Object {
    $_['Original Name'] -eq 'ser2pl.inf' -and $_['Driver Version'] -match '(^|\s)3\.3\.11\.152$'
} | Select-Object -First 1
if (-not $pkg) { throw 'Prolific 3.3.11.152 driver package not found on this PC' }
$oemInf = $pkg['Published Name']
if ($oemInf -notmatch '^oem[0-9]+\.inf$') { throw "unexpected published name '$oemInf'" }
Remove-InRepo $driverDir
New-Item -ItemType Directory -Force -Path $driverDir | Out-Null
& pnputil /export-driver $oemInf $driverDir
if ($LASTEXITCODE -ne 0) { throw "pnputil /export-driver $oemInf failed (exit $LASTEXITCODE)" }
foreach ($f in 'ser2pl.inf', 'ser2pl64.sys', 'ser2pl.cat') {
    if (-not (Test-Path -LiteralPath (Join-Path $driverDir $f))) { throw "$f missing in the exported driver" }
}
# The catalog must be signed, and the driver binary must verify against it
# (Valid also covers a .sys that is signed only through the system catalog).
foreach ($f in 'ser2pl.cat', 'ser2pl64.sys') {
    $sig = Get-AuthenticodeSignature -LiteralPath (Join-Path $driverDir $f)
    if ($sig.Status -ne 'Valid') { throw "$f signature is $($sig.Status), not Valid" }
}
Write-Host "  ok  $oemInf ($($pkg['Original Name']) $($pkg['Driver Version'])), ser2pl.cat and ser2pl64.sys signatures Valid"

# 3 ---------------------------------------------------------------------
Step 'Firmware V1'
# The packer's errors are ignored by the Makefile (-$(MY_PYTHON) ...), so
# delete the old packed image first and verify the new one below.
$packed = Join-Path $v1dir 'firmware_uvk5_v1.packed.bin'
Remove-InRepo (Join-Path $v1dir 'build')
if (Test-Path -LiteralPath $packed) { Remove-Item -LiteralPath $packed }
Invoke-Bash 'platform/uv-k5-v1-dp32g030' 'make'
Step 'Firmware V3'
Remove-InRepo (Join-Path $v3dir 'build')
Invoke-Bash 'platform/uv-k5-v3-py32f071' 'make'

# 4 ---------------------------------------------------------------------
Step 'Version marker'
# A release must come from a clean commit: both images carry exactly this commit.
$latin1 = [Text.Encoding]::GetEncoding(28591)   # byte-for-byte
$marker = "SCOUT-VERSION:$baseVersion+$commit"
foreach ($bin in (Join-Path $v1dir 'firmware_uvk5_v1.bin'), (Join-Path $v3dir 'firmware_uvk5_v3.bin')) {
    if ($latin1.GetString([IO.File]::ReadAllBytes($bin)).IndexOf($marker, [StringComparison]::Ordinal) -lt 0) {
        throw "marker $marker missing in $bin"
    }
    Write-Host "  ok  $marker in $bin"
}
# The packed V1 image (what K5TOOL flashes) must be exactly the current raw
# image packed again: repack it and compare byte for byte.
$packVersion = (& git -C $repo rev-parse --short HEAD).Trim()
$repack = Join-Path $env:TEMP 'scout-v1-repack.bin'
& python (Join-Path $v1dir 'fw-pack.py') (Join-Path $v1dir 'firmware_uvk5_v1.bin') SCOUT $packVersion $repack
if ($LASTEXITCODE -ne 0) { throw 'fw-pack.py failed' }
if (-not (Test-Path -LiteralPath $packed) -or (Get-FileHash -LiteralPath $packed).Hash -ne (Get-FileHash -LiteralPath $repack).Hash) {
    throw 'firmware_uvk5_v1.packed.bin does not match the raw V1 build'
}
Remove-Item -LiteralPath $repack     # one named file, never a folder
Write-Host "  ok  firmware_uvk5_v1.packed.bin matches the raw image"

# 5 ---------------------------------------------------------------------
Step 'Emulator tests + manual screens'
Invoke-Bash 'platform/host-emulator' 'make test && make check'

# 6 ---------------------------------------------------------------------
Step 'Manual PDF'
Remove-InRepo $work
New-Item -ItemType Directory -Path $work | Out-Null
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repo 'docs\manual\build.ps1') -OutDir $work
if ($LASTEXITCODE -ne 0) { throw 'manual build failed' }
$manualPdf = Join-Path $work "Handleiding-Scout-porto-$baseVersion.pdf"
if (-not (Test-Path -LiteralPath $manualPdf)) { throw "manual not found: $manualPdf" }

# 7 ---------------------------------------------------------------------
Step 'Flasher tests'
& python -m pytest (Join-Path $flasher 'tests') -q
if ($LASTEXITCODE -ne 0) { throw 'flasher tests failed' }

# 8 ---------------------------------------------------------------------
Step 'Release assets'
Remove-InRepo $out
$makeAssets = Join-Path $repo 'tools\make_release_assets.py'
& python $makeAssets --version $Version --commit $commit --date $date `
    --flasher-version $flasherVersion --min-flasher $MinFlasher `
    --v1 $packed --v3 (Join-Path $v3dir 'firmware_uvk5_v3.bin') --manual $manualPdf --out $out
if ($LASTEXITCODE -ne 0) { throw 'make_release_assets.py failed' }

# 9 ---------------------------------------------------------------------
Step 'ScoutFlasher.exe'
Remove-InRepo (Join-Path $flasher 'build')
Remove-InRepo (Join-Path $flasher 'dist')
New-Item -ItemType Directory -Path (Join-Path $flasher 'build') | Out-Null
# The commit and date the EXE shows (sf/buildinfo.py reads it; gitignored).
$buildInfo = Join-Path $flasher 'sf\_build_info.py'
[IO.File]::WriteAllText($buildInfo,
    "# Generated by tools/release.ps1 for this build only; never commit it.`n" +
    "COMMIT = `"$commit`"`nBUILD_DATE = `"$date`"`n", $utf8)
# Windows file properties (scout-flasher.spec picks this file up when it exists).
$nums = @($flasherVersion.Split('.') | ForEach-Object { [int]$_ })
while ($nums.Count -lt 4) { $nums += 0 }
$vers = '({0}, {1}, {2}, {3})' -f $nums
$fileVersion = $nums -join '.'
$dash = [char]0x2013
$versionInfo = @"
# -*- coding: utf-8 -*-
# Generated by tools/release.ps1 (ScoutFlasher $flasherVersion, commit $commit).
VSVersionInfo(
  ffi=FixedFileInfo(
    filevers=$vers,
    prodvers=$vers,
    mask=0x3f,
    flags=0x0,
    OS=0x40004,
    fileType=0x1,
    subtype=0x0,
    date=(0, 0)
  ),
  kids=[
    StringFileInfo([
      StringTable('040904B0', [
        StringStruct('CompanyName', 'DeDuben7'),
        StringStruct('FileDescription', 'ScoutFlasher $dash UV-K5 firmware updater'),
        StringStruct('FileVersion', '$fileVersion'),
        StringStruct('InternalName', 'ScoutFlasher'),
        StringStruct('LegalCopyright', 'Apache-2.0'),
        StringStruct('OriginalFilename', 'ScoutFlasher.exe'),
        StringStruct('ProductName', 'ScoutFlasher'),
        StringStruct('ProductVersion', '$flasherVersion ($commit)'),
        StringStruct('Comments', 'commit $commit')])
    ]),
    VarFileInfo([VarStruct('Translation', [1033, 1200])])
  ]
)
"@
[IO.File]::WriteAllText((Join-Path $flasher 'build\version_info.txt'), $versionInfo.Replace("`r`n", "`n"), $utf8)
try {
    & python -m PyInstaller --noconfirm --clean --log-level WARN `
        --distpath (Join-Path $flasher 'dist') --workpath (Join-Path $flasher 'build') `
        (Join-Path $flasher 'scout-flasher.spec')
    if ($LASTEXITCODE -ne 0) { throw 'PyInstaller failed' }
} finally {
    # Runs from source show "dev" again.
    Remove-Item -LiteralPath $buildInfo -ErrorAction SilentlyContinue
}
$exe = Join-Path $out 'ScoutFlasher.exe'
Copy-Item -LiteralPath (Join-Path $flasher 'dist\ScoutFlasher.exe') -Destination $exe
& python $makeAssets --out $out --exe $exe
if ($LASTEXITCODE -ne 0) { throw 'make_release_assets.py --exe failed' }

# 10 --------------------------------------------------------------------
Step "Release $Version"
$present = @(Get-ChildItem -LiteralPath $out -Force | ForEach-Object { $_.Name } | Sort-Object)
if (($present -join '|') -ne (($assetNames | Sort-Object) -join '|')) {
    throw "release\$Version must hold exactly $($assetNames -join ', '); it holds $($present -join ', ')"
}
$info = [IO.File]::ReadAllText((Join-Path $out 'scout-update.json')) | ConvertFrom-Json
$exeHash = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLower()
$zipHash = (Get-FileHash -LiteralPath (Join-Path $out 'scout-firmware.zip') -Algorithm SHA256).Hash.ToLower()
if ($info.release -ne $Version -or $info.commit -ne $commit -or $info.exe.sha256 -ne $exeHash -or
    $info.firmware_zip.sha256 -ne $zipHash) {
    throw 'scout-update.json does not match the assets'
}
if ((Get-Commit) -ne $commit) { throw "The working tree changed during the build ($(Get-Commit)); the assets say $commit" }
Remove-InRepo $work
Show-Assets
$vi = (Get-Item -LiteralPath $exe).VersionInfo
Write-Host "  ScoutFlasher.exe: FileVersion $($vi.FileVersion), ProductVersion $($vi.ProductVersion), $($vi.Comments)"

if (-not $Publish) {
    Write-Host "`nBuilt, not published. Publish with: tools\release.ps1 -Version $Version -Publish"
    return
}

# 11 --------------------------------------------------------------------
Step "Publish v$Version to $ghRepo"
$notes = Assert-PublishReady
$head = (& git -C $repo rev-parse HEAD).Trim()
if ((Get-Commit) -ne $commit) { throw "HEAD is no longer $commit" }
Show-Assets
if ($Yes) {
    Write-Host 'Confirmed with -Yes'
} elseif ("$(Read-Host "Type yes to publish v$Version publicly")".Trim() -cne 'yes') {
    throw "Not published. The assets stay in $out"
}

# A failed publish can simply be run again: the tag on HEAD is reused and the
# pushes change nothing when origin already has the commit and the tag.
$again = 'Running the same command again is safe: the tag on HEAD is reused and the pushes are idempotent.'
$tagged = & git -C $repo rev-parse -q --verify "refs/tags/v$($Version)^{commit}"
if ($tagged) {
    if ("$tagged".Trim() -ne $head) {
        throw ("Tag v$Version already exists on $("$tagged".Trim()), not on HEAD $head. If it was never pushed, " +
               "remove it (git tag -d v$Version) and run this again; otherwise use a new version, " +
               "for example $baseVersion-rc2.")
    }
    Write-Host "  tag v$Version is already on HEAD"
} else {
    & git -C $repo tag -a "v$Version" -m "Release $Version"
    if ($LASTEXITCODE -ne 0) { throw "git tag v$Version failed" }
}
Invoke-GitRemote push origin HEAD:main
if ($LASTEXITCODE -ne 0) { throw "git push origin HEAD:main failed. $again" }
Invoke-GitRemote push origin "v$Version"
if ($LASTEXITCODE -ne 0) { throw "git push origin v$Version failed. $again" }

[IO.File]::WriteAllText($notesFile, $notes + "`n", $utf8)
$ghArgs = @('release', 'create', "v$Version") + @($assetNames | ForEach-Object { Join-Path $out $_ }) +
          @('--repo', $ghRepo, '--title', "Scout $Version", '--notes-file', $notesFile, '--verify-tag')
if ($Prerelease) { $ghArgs += '--prerelease' } else { $ghArgs += '--latest' }
& gh @ghArgs
if ($LASTEXITCODE -ne 0) {
    throw ("gh release create failed. The commit and tag are already on origin. $again " +
           "If gh left a draft or half-uploaded release v$Version, delete it on https://github.com/$ghRepo/releases first.")
}

if ($Prerelease) {
    Write-Host "`nPre-release v$Version published; Latest is unchanged, so ScoutFlasher does not see it."
    Write-Host "Assets: https://github.com/$ghRepo/releases/download/v$Version/"
    Write-Host "Test with SCOUTFLASHER_UPDATE_URL=https://github.com/$ghRepo/releases/download/v$Version/"
} else {
    Write-Host "`nPublished v$Version as Latest. The permanent link:"
    Write-Host "https://github.com/$ghRepo/releases/latest/download/ScoutFlasher.exe"
}
