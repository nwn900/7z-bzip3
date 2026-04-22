param(
  [string]$Version = "26.0.1",
  [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
  [switch]$BuildMsi
)

$ErrorActionPreference = "Stop"

function Get-MsiVersion {
  param([string]$InputVersion)

  $normalized = $InputVersion.Trim()
  if ($normalized.StartsWith("v")) {
    $normalized = $normalized.Substring(1)
  }

  $numeric = [regex]::Match($normalized, "^\d+(?:\.\d+){0,3}").Value
  if ([string]::IsNullOrWhiteSpace($numeric)) {
    throw "Could not derive an MSI version from '$InputVersion'."
  }

  $parts = @()
  foreach ($piece in $numeric.Split(".")) {
    if ($parts.Count -ge 3) {
      break
    }
    $parts += [int]$piece
  }

  while ($parts.Count -lt 3) {
    $parts += 0
  }

  return ($parts -join ".")
}

$displayVersion = $Version.Trim()
if ($displayVersion.StartsWith("v")) {
  $displayVersion = $displayVersion.Substring(1)
}

$msiVersion = Get-MsiVersion -InputVersion $Version

$artifactsRoot = Join-Path $RepoRoot "artifacts"
$stageRoot = Join-Path $artifactsRoot "stage"
$portableRoot = Join-Path $stageRoot "7zip-bzip3-x64"

Remove-Item -LiteralPath $artifactsRoot -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $portableRoot | Out-Null

$filesToStage = @(
  @{ Source = "CPP\7zip\Bundles\Format7zF\x64\7z.dll"; Target = "7z.dll" },
  @{ Source = "CPP\7zip\Bundles\Alone2\x64\7zz.exe"; Target = "7z.exe" },
  @{ Source = "CPP\7zip\UI\GUI\x64\7zG.exe"; Target = "7zG.exe" },
  @{ Source = "CPP\7zip\Bundles\Fm\x64\7zFM.exe"; Target = "7zFM.exe" },
  @{ Source = "CPP\7zip\UI\Explorer\x64\7-zip.dll"; Target = "7-zip.dll" },
  @{ Source = "CPP\7zip\UI\Explorer\x86\7-zip.dll"; Target = "7-zip32.dll" },
  @{ Source = "CPP\7zip\Bundles\SFXWin\x64\7z.sfx"; Target = "7z.sfx" },
  @{ Source = "CPP\7zip\Bundles\SFXCon\x64\7zCon.sfx"; Target = "7zCon.sfx" },
  @{ Source = "README.md"; Target = "README.md" },
  @{ Source = "DOC\License.txt"; Target = "7zip-LICENSE.txt" },
  @{ Source = "BZip3-LICENSE.txt"; Target = "BZip3-LICENSE.txt" }
)

foreach ($item in $filesToStage) {
  $sourcePath = Join-Path $RepoRoot $item.Source
  if (-not (Test-Path -LiteralPath $sourcePath)) {
    throw "Missing required file: $sourcePath"
  }

  $targetPath = Join-Path $portableRoot $item.Target
  Copy-Item -LiteralPath $sourcePath -Destination $targetPath -Force
}

$portableZip = Join-Path $artifactsRoot ("7zip-bzip3-x64-{0}-portable.zip" -f $displayVersion)
Compress-Archive -Path $portableRoot -DestinationPath $portableZip -Force

if ($BuildMsi) {
  $candle = Get-Command candle.exe -ErrorAction Stop
  $light = Get-Command light.exe -ErrorAction Stop
  $wxsPath = Join-Path $RepoRoot "installer\7zip-bzip3.wxs"
  $wixObj = Join-Path $artifactsRoot "7zip-bzip3.wixobj"
  $msiPath = Join-Path $artifactsRoot ("7zip-bzip3-x64-{0}.msi" -f $displayVersion)

  & $candle.Source "-nologo" "-dStageDir=$portableRoot" "-dVersion=$msiVersion" "-out" $wixObj $wxsPath
  if ($LASTEXITCODE -ne 0) {
    throw "candle.exe failed with exit code $LASTEXITCODE"
  }

  & $light.Source "-nologo" "-sval" "-out" $msiPath $wixObj
  if ($LASTEXITCODE -ne 0) {
    throw "light.exe failed with exit code $LASTEXITCODE"
  }
}

$hashTargets = Get-ChildItem -LiteralPath $artifactsRoot -File |
  Where-Object { $_.Extension -in @(".zip", ".msi") } |
  Sort-Object Name

$hashLines = foreach ($file in $hashTargets) {
  $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
  "{0} *{1}" -f $hash, $file.Name
}

Set-Content -LiteralPath (Join-Path $artifactsRoot "SHA256SUMS.txt") -Value $hashLines -Encoding ascii

Write-Host "Portable root: $portableRoot"
Write-Host "Artifacts:"
Get-ChildItem -LiteralPath $artifactsRoot -File | Sort-Object Name | ForEach-Object {
  Write-Host (" - {0}" -f $_.Name)
}
