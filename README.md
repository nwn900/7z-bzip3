# 7-Zip BZip3

Windows x64 fork of 7-Zip 26.00 with [`bzip3`](https://github.com/iczelia/bzip3)
integrated as a first-class archive format.

## What changed

- Adds native `.bz3`, `.bzip3`, and `.tbz3` support to `7z.dll`, `7z.exe`,
  `7zG.exe`, and `7zFM.exe`.
- Makes `bzip3` the default GUI compression choice for single-file compression.
- Adds Explorer quick actions for stock `.7z` archives and for BZip3 archives.
- Uses `.bz3` for single-file quick compression and `.tar.bz3` for folders or
  multi-select, while keeping the normal 7z/zip/xz/bzip2 choices available.
- Ships a GitHub Actions release pipeline that builds a portable package and an
  MSI installer with shell integration.

## Build

### Local x64 build

```powershell
cmd /c '"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" && nmake'
```

Run that from `CPP\7zip`.

### Package portable release

```powershell
.\scripts\Package-Release.ps1 -Version 26.0.1
```

### Smoke test packaged binaries

```powershell
.\scripts\Smoke-Test.ps1
```

### GitHub Actions release

Push a tag such as `v26.0.1`. The workflow in
`.github/workflows/release.yml` builds:

- `7zip-bzip3-x64-<version>-portable.zip`
- `7zip-bzip3-x64-<version>.msi`
- `SHA256SUMS.txt`

## Notes

- BZip3 is a single-stream format. For folders or multi-file shell quick
  actions, this fork creates a `.tar.bz3` archive.
- The installer registers the standard 7-Zip shell extension hooks so Windows
  Explorer gets pack and unpack context-menu entries.
- The release package includes both the 64-bit shell extension and the 32-bit
  companion DLL used by 32-bit shell hosts on x64 Windows.

## Licensing

- 7-Zip source remains under the licenses described in
  `DOC\License.txt`.
- The integrated BZip3 code is LGPL v3; see `BZip3-LICENSE.txt`.
