# Rampage Xbox asset-preparation GUI

This is a Windows file-preparation tool for the original Xbox executable. It is
not a PC-game installer. It contains no original game assets and makes no network
requests. A one-file, windowed EXE belongs at the root of the extracted release:

```
Prepare Rampage Xbox.exe
Rampage Xbox/
  default.xbe
  data/                 # Generated from the user's PS2 input
    RAMPAGE.VOL
    RAMPAGE.ELF
    SYSTEM.CNF          # Copied when supplied
  wii/                  # Optional user-owned Wii input
    Game/
    Shell/
    Audio/
  asset-manifest.json   # Generated local file hashes, no input paths or keys
licenses/
```

## Required inputs

The current native Xbox implementation requires the PS2 base assets. Supported
PS2 releases are Europe SLES-53800 version 1.00 and USA SLUS-21323 version 1.01.
The user chooses an ISO9660 image with 2048-byte logical sectors, an extracted
folder, or RAMPAGE.VOL beside its PS2 executable (RAMPAGE.ELF, SLES_538.00, or
SLUS_213.23). Keep SYSTEM.CNF beside the files when available to preserve region.
Raw BIN, compressed PS2 images, and reading physical drives are not supported.

PS2 data is copied as RAMPAGE.VOL plus RAMPAGE.ELF, not unpacked into loose bundle
files. This matches src/iso.c, src/vol.c, and data_elf consumers of the native game.
The source's PVOL name-size field overstates the name-block span; the validator
uses actual referenced name terminators to avoid rejecting the genuine release.
The structural release check uses the native 352-file / 1,342,827,563-byte-end
signature. It is not a cryptographic certification that the source is pristine.

The optional Wii picker accepts a single-file WBFS or ISO, or a full Dolphin-
extracted disc folder. Game/Shell/Audio are needed; Game-only folders are rejected.
The GUI labels Wii preparation experimental because no complete real Wii source
was available for end-to-end validation. Wii content does not replace the PS2
base. RPGP5D is the source-backed reference release; other RPG?5D disc regions
pass identity checking but are not independently verified. Wii Video and Wii
Rampage.elf are not used or copied. Split WBFS (.wbfs plus .wbf1) is rejected with
an explicit message; use a single image or a full extracted folder.

A small Rust helper uses the maintained MIT/Apache-2.0 nod 1.4.4 library for Wii
partition reading/decryption. There is no key prompt, downloaded firmware,
external program installation, or private-key file in this package. The helper
checks Wii identity, hash-validates reads, parses bounded FST entries, rejects
unsafe/duplicate names, and copies only immediate Game/Shell/Audio files into a
fresh staging directory. It does not boot, emulate, or distribute the disc.

## Safety and transactional behavior

- All input is read-only. Links/junctions, path traversal, unsupported FATX names,
  case collisions, flat Wii basename collisions, invalid extents, short reads,
  wrong disc identity/version, and incomplete Wii selections are rejected.
- Existing data is replaced only after explicit GUI confirmation. If Wii is
  unchecked, existing Wii files are preserved. Saves, config, and default.xbe are
  preserved byte for byte.
- A process lock prevents simultaneous preparation of the same package.
- All files are copied into a sibling staging directory and SHA-256 verified from
  disk before commit. Adequate staging-space headroom is checked first.
- Cancellation is accepted before commit. Commit uses two same-volume directory
  renames with rollback on failure. This is rollback-capable, not a single
  crash-atomic exchange on Windows. The final short commit cannot be canceled.
- If power/process interruption occurs between renames, the old folder remains
  in `._rampage-prep-*/backup`. Recover can restore it when the target is missing;
  recovery never deletes an existing game folder or backup. Leftovers are kept
  until explicitly inspected/moved. No unsupported automatic cleanup claim.
- This is local input validation, not protection against an attacker actively
  modifying the source or package during preparation. Close tools that edit them.

## Build integration (Windows x64)

Entry point: `gui.py`. `asset_prep.py` is a development compatibility launcher.
Python 3.12 with its bundled Tcl/Tk and PyInstaller 6.16.0 are recommended.
Build Rust using a supported current stable compiler. Root dependencies are exact
versions. Generate and retain Cargo.lock before publishing; use --locked for
subsequent builds. The assigned environment lacked Cargo, so lockfile generation
and compilation must be verified by the packaging job.

```
python -m pip install -r requirements-build.txt
cargo generate-lockfile --manifest-path disc_reader/Cargo.toml
cargo test --locked --manifest-path disc_reader/Cargo.toml
cargo build --locked --release --manifest-path disc_reader/Cargo.toml
python -m unittest discover -s tests -v
python collect_licenses.py licenses
python -m PyInstaller --clean --noconfirm --onefile --windowed --name "Prepare Rampage Xbox" --add-binary "disc_reader/target/release/rampage-disc-reader.exe;." gui.py
```

Preserve nod's included MIT and Apache notices, collect notices from the actual
Python/Tcl/Tk/PyInstaller environment, and include the project GPL license and
corresponding source/build instructions in the release. Rust transitive license
notices also need collection from the locked dependency graph (for example
`cargo-about` with a reviewed configuration). This repository does not claim that
the two top-level nod licenses alone cover every dependency.

Windowed EXE CI smoke test (status file is authoritative, not stdout):

```
$proc = Start-Process -FilePath ".\dist\Prepare Rampage Xbox.exe" -ArgumentList @('--smoke-test','--status-file',"$PWD\smoke.json") -Wait -PassThru
if ($proc.ExitCode -ne 0) { throw 'GUI smoke failed' }
$result = Get-Content .\smoke.json -Raw | ConvertFrom-Json
if (-not $result.ok) { throw ($result | ConvertTo-Json -Depth 8) }
```

The smoke test briefly opens the real GUI. It uses tiny synthetic data in a fresh
temporary folder, validates missing input, double-click guarding, worker/staging
completion, success notification, declined overwrite, and clean close. It never
reads retail inputs. It patches fixture expectations only inside smoke mode.

Source GUI for actual user-flow acceptance:

```
python gui.py --package /path/to/extracted/release
```

Use the file dialog to select the full owned PS2 ISO; observe progress and success,
then compare generated hashes with the same files inside the source ISO. The
`--package` override is for test/dev packaging; normal users run the root EXE.
A headless path exercises the identical engine but is not a GUI acceptance test:

```
python gui.py --headless --package RELEASE --ps2 OWNED.iso
```

## Verification scope

- 50 synthetic Python tests passed on Linux, including extraction, cancellation,
  overwrite protection, recovery locking, malformed input, and license checks.
- The complete owned USA PS2 ISO (4,627,202,048 bytes) was selected through the
  GUI and prepared successfully on Linux. The three output file hashes matched
  the original disc references; default.xbe was unchanged and no staging files
  remained. Launching from a different working directory still used the tool's
  own package folder.
- Windows Actions compiles and tests the optional reader, builds the frozen EXE,
  and runs a synthetic GUI test including the bundled helper. Consult that run's
  status and gui-smoke.json for the result. Retail disc data is never uploaded
  to CI, so this is separate from the real-input Linux GUI test.
- No full real Wii ISO/WBFS was available. Optional Wii is not end-to-end verified.
- Physical Xbox gameplay is not certified by these preparation checks.
