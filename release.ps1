# Makes the release zip, dist\Aladdin-Collection-v<version>-win64.zip, holding only what is needed to play:
# the executable, the player's readme and the licence (MIT: its notice goes with every copy), and librashader.dll
# with its licence and the bundled slang shader presets (shaders\, with their README.txt and licences).
# No ROMs, settings, saves, screenshots or shortcuts.
# Build first with build.bat.   Usage:  powershell -ExecutionPolicy Bypass -File release.ps1
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$exe = Join-Path $root "build\bin\AladdinCollection.exe"
if (-not (Test-Path $exe)) { throw "Build first with build.bat" }
$version = (Select-String -Path (Join-Path $root "src\app\app.h") -Pattern 'kAppVersion = "([^"]+)"').Matches[0].Groups[1].Value
New-Item -ItemType Directory -Force (Join-Path $root "dist") | Out-Null
$zipPath = Join-Path $root "dist\Aladdin-Collection-v$version-win64.zip"

Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$files = @(
  @($exe, "AladdinCollection.exe"),
  @((Join-Path $root "readme.txt"), "readme.txt"),
  @((Join-Path $root "LICENSE"), "LICENSE")
)
# Slang shader support: librashader.dll (loaded at run time) with its licence, and the bundled presets
$bin = Split-Path $exe
if (Test-Path (Join-Path $bin "librashader.dll")) {
  $files += ,@((Join-Path $bin "librashader.dll"), "librashader.dll")
  $files += ,@((Join-Path $bin "librashader-LICENSE.md"), "librashader-LICENSE.md")
  $shaders = Join-Path $bin "shaders"
  Get-ChildItem $shaders -Recurse -File | ForEach-Object {
    $files += ,@($_.FullName, ("shaders/" + $_.FullName.Substring($shaders.Length + 1).Replace("\", "/")))
  }
} else { Write-Warning "librashader.dll not in build\bin: the zip has no slang shader support" }
$stream = [System.IO.File]::Open($zipPath, [System.IO.FileMode]::Create)
$zip = New-Object System.IO.Compression.ZipArchive($stream, [System.IO.Compression.ZipArchiveMode]::Create)
try {
  foreach ($f in $files) {
    [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $f[0], "Aladdin Collection/" + $f[1],
      [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
  }
} finally {
  $zip.Dispose()
  $stream.Dispose()
}
Write-Host "Release zip: $zipPath"
