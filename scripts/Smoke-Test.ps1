param(
  [string]$PortableRoot = (Join-Path (Resolve-Path (Join-Path $PSScriptRoot "..")).Path "artifacts\stage\7zip-bzip3-x64")
)

$ErrorActionPreference = "Stop"

$binRoot = (Resolve-Path $PortableRoot).Path
$exe = Join-Path $binRoot "7z.exe"
$gui = Join-Path $binRoot "7zFM.exe"

foreach ($required in @(
  $exe,
  $gui,
  (Join-Path $binRoot "7z.dll"),
  (Join-Path $binRoot "7-zip.dll"),
  (Join-Path $binRoot "7-zip32.dll")
)) {
  if (-not (Test-Path -LiteralPath $required)) {
    throw "Missing required artifact: $required"
  }
}

$infoOutput = (& $exe i) -join "`n"
if ($LASTEXITCODE -ne 0) {
  throw "7z.exe i failed."
}
if ($infoOutput -notmatch "bzip3\s+bz3") {
  throw "bzip3 format was not detected in 7z.exe i output."
}

$work = Join-Path ([System.IO.Path]::GetTempPath()) ("7zip-bzip3-smoke-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $work | Out-Null

try {
  Push-Location $work

  "alpha bravo charlie" | Set-Content -LiteralPath "sample.txt" -Encoding ascii

  & $exe a sample.bz3 sample.txt -tbzip3 -y | Out-Null
  if ($LASTEXITCODE -ne 0) {
    throw "bzip3 archive creation failed."
  }

  & $exe x sample.bz3 -oout-bz3 -y | Out-Null
  if ($LASTEXITCODE -ne 0) {
    throw "bzip3 extraction failed."
  }

  & $exe a sample.7z sample.txt -t7z -y | Out-Null
  if ($LASTEXITCODE -ne 0) {
    throw "7z archive creation failed."
  }

  & $exe x sample.7z -oout-7z -y | Out-Null
  if ($LASTEXITCODE -ne 0) {
    throw "7z extraction failed."
  }

  $bz3Text = (Get-Content -LiteralPath "out-bz3\sample" -Raw)
  $sevenZipText = (Get-Content -LiteralPath "out-7z\sample.txt" -Raw)

  if ($bz3Text -ne "alpha bravo charlie`r`n") {
    throw "Unexpected bzip3 extraction content."
  }
  if ($sevenZipText -ne "alpha bravo charlie`r`n") {
    throw "Unexpected 7z extraction content."
  }
}
finally {
  Pop-Location
  Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host "Smoke test passed."
