Rampage Xbox hardware-test package
=================================

1. Extract the entire download ZIP to a new, empty writable folder on your
   Windows PC. Do not merge it over an older Xbox installation.
2. Run Prepare Rampage Xbox.exe from the ZIP's root folder.
3. Select your own supported PS2 disc image. The current port needs PS2 data
   even when Wii extras are enabled. Supported PS2 releases are Europe
   SLES-53800 v1.00 and USA SLUS-21323 v1.01.
4. To include the Wii roster and Dallas, select your own complete Wii disc
   image or a complete extracted Wii disc folder as offered by the tool.
   The tool explains supported formats. A Game-only folder is not a full
   Wii input; Shell and Audio are required as well.
5. Wait for the tool to finish and show success. Original disc images are
   not changed. All prepared assets remain local to your PC.
6. Transfer the entire "Rampage Xbox" folder to your homebrew-enabled
   original Xbox, then launch its default.xbe. Keep its data and wii folders
   beside default.xbe, with their contents and directory structure intact.

Layout after setup
------------------
Prepare Rampage Xbox.exe
Rampage Xbox/
  default.xbe
  data/
    RAMPAGE.VOL
    RAMPAGE.ELF
    SYSTEM.CNF
  wii/                 (only when Wii extras were selected)
    Game/
    Shell/
    Audio/

This download contains no original game assets. Do not share the folder or
ZIP after adding your disc files. No PC version of the game is installed;
this EXE only prepares assets for the Xbox executable.

Hardware checks
---------------
- First boot: legal/autosave screens, movies, main menu and profile creation.
- PS2-only: select a monster, enter a campaign level, move/jump/attack, check
  sound/music, Pause/Resume, quit to the menu, start another level.
- With Wii extras: confirm the 40-monster roster, Dallas and the fifth star,
  then check Wii-monster sounds and Dallas loading/gameplay.
- Reboot and confirm your profile persists. Test your real controller and
  monitor/audio setup, plus longer play and level transitions.

Xbox saves/settings/logs live in E:\UDATA\RampageRE. Existing settings take
precedence over packaged defaults. If an earlier session set wii_data=none,
Wii files will be ignored: back up rampage.cfg, then remove just that setting
or set wii_data=wii. Keep your profile files. The package does not overwrite
console saves and the preparation tool does not connect to the console.

The runtime is experimental. Automated preparation checks and emulator
observations do not certify physical-controller behavior, all levels,
long-session stability, AV sync or real-Xbox performance. Report the build
ID, Xbox memory configuration, video mode, controller, disc region, Wii
selection and exact reproduction steps. Review logs for private paths
before sharing. Never attach disc images or original game assets.

Security and provenance
-----------------------
The Windows tool is unsigned. A checksum verifies that a file matches this
build; it is not an independent security certification. If Windows or an
antivirus blocks it, inspect and report the exact warning. Do not disable
security software. No runtime dependencies need to be installed separately
for the preparation tool.

BUILD-INFO.json identifies the exact source commit and XBE. The XBE is the
preserved validated ordinary binary, not a fresh Actions rebuild. Actions
builds the Windows preparation EXE and assembles this package. Source and
dependency information are in the same repository/build artifacts.

Unofficial fan project, not affiliated with the game's rights holders.
You must supply your own supported discs. See LICENSES for notices.
