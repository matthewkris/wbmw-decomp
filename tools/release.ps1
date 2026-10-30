# Publishes a Windows release: builds the native game from a clean, pushed main, packs the zip,
# checks it carries no ROM, plays it from an empty folder, then sets the repository's About text
# and creates the GitHub release. Stops at the first failure, before anything is published.
#
#   powershell -ExecutionPolicy Bypass -File tools\release.ps1 v1.0.1 [-DryRun]
#
# -DryRun does everything but publish. Needs MSYS2 (the build, and gh: `gh auth login` once) and
# your ROM in rom/. The texts are in tools/release/. The zip is left in work/release/.
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [switch]$DryRun
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
if ($Version -notmatch '^v\d+\.\d+\.\d+$') { throw "Version must look like v1.0.1, not $Version" }

$gh = 'C:\msys64\ucrt64\bin\gh.exe'
$rom = Join-Path $root 'rom\Wonder Boy in Monster World (USA, Europe).md'
$python = (Get-Command python).Source
$windowsPath = 'C:\Windows\System32;C:\Windows'
$pathBefore = $env:PATH
$env:PATH = "C:\msys64\ucrt64\bin;C:\msys64\usr\bin;" + $env:PATH
$env:TEMP = $env:TMP = $env:TMPDIR = Join-Path $root 'work'   # gcc cannot use the default here

function Check($ok, $what) { if (-not $ok) { throw "Stopped: $what" }; Write-Output "ok: $what" }
function Run($exe, $arguments, $dir, $log) {                   # exit code, with only Windows on the PATH
    $env:PATH = $windowsPath
    try { (Start-Process $exe -ArgumentList $arguments -WorkingDirectory $dir -Wait -PassThru -NoNewWindow -RedirectStandardError $log).ExitCode }
    finally { $env:PATH = "C:\msys64\ucrt64\bin;C:\msys64\usr\bin;" + $pathBefore }
}

# ---- what is released is what is pushed
Check (Test-Path $rom) 'the ROM is in rom/'
Check ((git status --porcelain) -eq $null) 'no uncommitted changes'
Check ((git rev-parse --abbrev-ref HEAD) -eq 'main') 'on main'
git fetch -q origin
Check ((git rev-parse HEAD) -eq (git rev-parse origin/main)) 'main is pushed'
Check (-not (git ls-remote --tags origin $Version)) "$Version is not taken"
if (-not $DryRun) {
    Check (Test-Path $gh) 'gh is installed'
    & $gh auth status 2>&1 | Out-Null
    Check ($LASTEXITCODE -eq 0) 'gh is logged in'
}

# ---- the build, and the zip's folder
& make -C native -j12 2>&1 | Select-Object -Last 1
Check ($LASTEXITCODE -eq 0) 'the native build'
$name = "wbmw-$Version-windows-x64"
$out = Join-Path $root 'work\release'
$dir = Join-Path $out $name
$zip = "$dir.zip"
Remove-Item -Recurse -Force $dir, $zip -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force (Join-Path $dir 'licences') | Out-Null
foreach ($f in 'wbmw.exe', 'SDL2.dll', 'libgcc_s_seh-1.dll', 'libstdc++-6.dll', 'libwinpthread-1.dll') {
    Copy-Item (Join-Path 'work\native' $f) $dir
}
Copy-Item 'tools\release\READ-ME-FIRST.txt' $dir
$licences = 'C:\msys64\ucrt64\share\licenses'
$copies = @{
    'LICENSE' = 'wbmw-LICENSE.txt'
    'third_party\ymfm\LICENSE' = 'ymfm-LICENSE.txt'
    'third_party\chips\LICENSE' = 'chips-LICENSE.txt'
    "$licences\SDL2\LICENSE.txt" = 'SDL2-LICENSE.txt'
    "$licences\gcc\COPYING3" = 'gcc-COPYING3.txt'
    "$licences\gcc\COPYING.RUNTIME" = 'gcc-COPYING.RUNTIME.txt'
    "$licences\winpthreads\COPYING" = 'winpthreads-COPYING.txt'
}
foreach ($c in $copies.GetEnumerator()) { Copy-Item $c.Key (Join-Path "$dir\licences" $c.Value) }

# ---- no ROM in it
& $python -I tools\romscan.py $rom (Join-Path $dir 'wbmw.exe')
Check ($LASTEXITCODE -eq 0) 'the program carries no long run of the ROM'
Compress-Archive -Path $dir -DestinationPath $zip

# ---- played from the zip, in an empty folder, with only Windows on the PATH
$test = Join-Path $out 'test'
Remove-Item -Recurse -Force $test -ErrorAction SilentlyContinue
try {
    Expand-Archive $zip $test
    $play = Join-Path $test $name
    $exe = Join-Path $play 'wbmw.exe'
    Check ((Run $exe @('--frames', '5') $play "$test\none.txt") -eq 2) 'no ROM: refused'
    New-Item -ItemType Directory (Join-Path $play 'rom') | Out-Null
    Copy-Item $rom (Join-Path $play 'rom')
    Check ((Run $exe @('--frames', '2000', '--mute', '--png-at', '1500') $play "$test\play.txt") -eq 0) '2,000 frames'
    Check (Test-Path (Join-Path $play 'frame_01500.png')) 'a picture'
    Check (-not (Select-String -Path "$test\play.txt" -Pattern 'warning' -Quiet)) 'the expected ROM'
    $env:PATH = $windowsPath
    $p = Start-Process $exe -ArgumentList '--windowed', '--mute' -WorkingDirectory $play -PassThru
    $env:PATH = "C:\msys64\ucrt64\bin;C:\msys64\usr\bin;" + $pathBefore
    Start-Sleep 5
    $p.Refresh()
    $alive = -not $p.HasExited
    $title = $p.MainWindowTitle
    if ($alive) { Stop-Process $p -Force; Wait-Process -Id $p.Id -ErrorAction SilentlyContinue }
    Check ($alive -and $title -match 'fps') "a window, running ($title)"
} finally {
    Remove-Item -Recurse -Force $test -ErrorAction SilentlyContinue   # it holds a copy of the ROM
}

$hash = (Get-FileHash $zip -Algorithm SHA256).Hash
Write-Output "$zip`n$((Get-Item $zip).Length) bytes, SHA-256 $hash"
if ($DryRun) { Write-Output 'Dry run: nothing published.'; exit 0 }

# ---- published
$topics = (Get-Content 'tools\release\topics.txt' | Where-Object { $_ }) -join ','
& $gh repo edit --description (Get-Content 'tools\release\about.txt' -Raw).Trim() --add-topic $topics
Check ($LASTEXITCODE -eq 0) 'the About text and topics'
& $gh release create $Version $zip --title $Version --notes-file 'tools\release\notes.md' --target main
Check ($LASTEXITCODE -eq 0) "the release $Version"
