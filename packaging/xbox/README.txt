RampageRE for the original Xbox, version 0.8.0
==============================================

1. Extract the entire download ZIP to a new, empty writable folder on your
   Windows PC. Do not merge it over an older Xbox installation.
2. Run Prepare Rampage Xbox.exe from the ZIP's root folder.
3. Select your own supported PS2 disc image. The port needs PS2 data even
   when Wii extras are enabled. Supported PS2 releases are Europe
   SLES-53800 v1.00 and USA SLUS-21323 v1.01.
4. To include the Wii roster and Dallas, tick the Wii box and select your
   own complete Wii disc image (a single WBFS or ISO) or a complete
   extracted Wii disc folder. A Game-only folder is not a full Wii input;
   Shell and Audio are required as well. The tool converts the Wii's sound
   bank and Dallas's models to the PS2's formats (a console has no time for
   that while it plays) and leaves out the Wii files the game never reads:
   about 65 MB are installed. This takes about a minute longer.
5. Wait for the tool to finish and show success. Original disc images are
   not changed. All prepared assets remain local to your PC.
6. Transfer the entire "Rampage Xbox" folder to your homebrew-enabled
   original Xbox (about 1.4 GB), then launch its default.xbe. Keep its data
   and wii folders beside default.xbe, with their contents and directory
   structure intact.

Layout after setup
------------------
Prepare Rampage Xbox.exe
Rampage Xbox/
  default.xbe
  asset-manifest.json  (the prepared files' hashes; the game does not read it)
  data/
    RAMPAGE.VOL
    RAMPAGE.ELF
    SYSTEM.CNF
    XBOXTEX.RXT        (optional, your own: see "Xbox button pictures")
  wii/                 (only when Wii extras were selected)
    Game/
    Shell/
    Audio/

This download contains no original game assets. Do not share the folder or
ZIP after adding your disc files. No PC version of the game is installed;
this EXE only prepares assets for the Xbox executable.

Picture and speed
-----------------
The picture follows the console's own video settings and cable: 640x480
interlaced or progressive, the same drawn for a 16:9 screen when Widescreen
is set on the dashboard, or 1280x720 when 720p is enabled there and an HD
cable is connected. The game is built for the stock 64 MB console.

Measured on a stock console with four monsters on screen (test builds of
this source, November 2026): 1280x720 runs at about 30 frames a second in
the PS2's cities and in most of Dallas with the Wii's monsters; Dallas's
two busiest blocks (Fountain Place, Downtown) drop to 26 or 27. 640x480
runs faster. The first time a sound, a damaged building part or a texture
is needed in a level the picture can hold for about a tenth of a second.

Xbox button pictures
--------------------
The game's button prompts are the PS2 disc's (Cross, Circle, ...). The
Xbox build shows pictures from data\XBOXTEX.RXT in their place when that
file is there. No disc has it and none is included: tools/xbox_glyphs.py
of the source makes one from your own PNG pictures. The preparation tool
keeps an XBOXTEX.RXT that is already in the data folder. The controller
diagram of the options screen is still the PS2's.

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

The runtime is experimental. The preparation tool was run end to end with
the USA PS2 disc and the USA Wii disc before this release; the packaged
default.xbe itself was built from the same source as the builds measured
on the console but has not been booted on one. Nothing here certifies all
levels, long-session stability or AV sync. Report the version, Xbox memory
configuration, video mode, controller, disc region, Wii selection and
exact reproduction steps. Review logs for private paths before sharing.
Never attach disc images or original game assets.

Security and provenance
-----------------------
The Windows tool is unsigned. A checksum verifies that a file matches this
build; it is not an independent security certification. If Windows or an
antivirus blocks it, inspect and report the exact warning. Do not disable
security software. No runtime dependencies need to be installed separately
for the preparation tool.

BUILD-INFO.json identifies the version, the exact source commit and the
XBE. The XBE was built from that commit's source on the maintainer's PC
(platform/xbox of the source), not by Actions. Actions builds the Windows
preparation EXE and assembles this package. Source and dependency
information are in the same repository/build artifacts.

Unofficial fan project, not affiliated with the game's rights holders.
You must supply your own supported discs. See LICENSES for notices.
