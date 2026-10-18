# How it works

Chessmaster Grandmaster Edition (2007) can import outside engines, but only ones that speak the
**WinBoard** (CECP) protocol. Modern engines such as CeylonDemon speak **UCI**. The setup does
by hand what you would otherwise have to do yourself, and works around two Chessmaster quirks.

## 1. PolyGlot translates between the two

[PolyGlot](https://hgm.nubati.net/cgi-bin/gitweb.cgi?p=polyglot.git) is an adapter: Chessmaster
starts `polyglot.exe` as if it were a WinBoard engine, and PolyGlot starts the UCI engine named in
`polyglot.ini` and translates in both directions.

```ini
[PolyGlot]
EngineDir     = C:\Chess\CeylonDemon
EngineCommand = CeylonDemon-3.3.exe
Book          = false     ; Chessmaster plays its own opening book for the engine
Resign        = false
Log           = false     ; true writes every message to LogFile, useful for debugging

[Engine]                  ; UCI options passed to the engine
Hash          = 128
Threads       = 2
Move Overhead = 50
```

Stock PolyGlot 1.4.70b does not work with Chessmaster, for two reasons, fixed in
[`polyglot/chessmaster.patch`](../polyglot/chessmaster.patch):

- **Chessmaster starts engines with `-t1`** (an option meant for its own engine, The King).
  PolyGlot treated any unknown argument as an error and quit at once. The game then looks normal
  for a while, because Chessmaster plays opening-book moves on the engine's behalf, and stalls with
  the engine's clock running when the book ends. Patched PolyGlot logs and ignores unknown
  arguments.
- **The working folder.** PolyGlot reads `polyglot.ini` from the current folder. Patched PolyGlot
  falls back to the `polyglot.ini` next to `polyglot.exe`.

## 2. The player file (`Data\Personalities\<name>.CMP`)

Each player in Chessmaster's list is a 3104-byte `.CMP` file; the file name is the player's
name. Importing a WinBoard engine through Chessmaster's own window creates one with rating 0 and the
generic robot picture, and the window does not let you change either. The fields the setup writes
(little-endian integers, NUL-padded strings):

| Offset | Size | Contents |
|---|---|---|
| 0 | 32 | `Chessmaster: Grandmaster Edition` (built-in players say `Chessmaster 10th Edition`) |
| 0x24 | 4 | 16 |
| 0x28 | 4 | 178 |
| 0x38 | 4 | **rating** (built-in: Chessmaster 2724, Anand 2700) |
| 0x4C | 4 | 99 |
| 192 | 30 | opening book, `CMX.OBK` |
| 452 | 30 | **picture** name in the picture archive, e.g. `CeylonDemon.BMP` (imports use `Winboard.BMP`) |
| 482 | 100 | style line |
| 582 | 1000 | biography |
| 1582 | 1000 | playing-style description |
| 2582 | 522 | **engine path**, lower case: `c:\chess\ceylondemon\polyglot.exe` |

All other bytes are zero, exactly as in a file created by Chessmaster's import.

## 3. The picture archive (`Data\TData\faces.idx` + `faces.dat`)

Player pictures are 76 x 97, 24-bit BMP files (22,172 bytes) packed into one archive:

- `faces.idx`: a 32-byte header (`u32 32`, `ChessmasterDat01`, `u32 count`, `u32 32`, `u32 0`),
  then `count` records of 80 bytes, sorted by name ignoring case:
  `name[64]`, `"PKZP"`, `u32 offset in faces.dat`, `u32 size`, `u32 packed size`.
- `faces.dat`: the same 32-byte header, then the pictures, each compressed with
  **PKWARE DCL implode** (as decoded by zlib's `contrib/blast`).

To add a picture, the setup inserts a record in sorted position and rewrites both files. It stores
the picture as a valid DCL stream of plain literals (no compression needed), which Chessmaster
reads like any other. [`tools/cmfaces.py`](../tools/cmfaces.py) can list, extract, check and add
pictures, and decodes all of Chessmaster's own.

## 4. Windows VirtualStore

Chessmaster predates User Account Control. When it writes into `Program Files` without
administrator rights, Windows silently redirects the write to
`%LOCALAPPDATA%\VirtualStore\Program Files (x86)\Ubisoft\Chessmaster Grandmaster Edition\...`, and
from then on Chessmaster reads that copy instead of the real file. So the setup:

- runs with administrator rights and edits the real files;
- also adds the picture to every VirtualStore copy of `faces.idx`/`faces.dat`, since such a copy
  hides the real one;
- deletes any VirtualStore copy of the `.CMP` file, for the same reason.

It does this for **every user profile** on the PC (from
`HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\ProfileList`), not just its own. On a standard
(non-administrator) Windows account, approving the setup's permission prompt runs it as the
administrator account, whose `%LOCALAPPDATA%` is not the player's.

## 5. Uninstalling

The setup copies itself to `C:\Chess\CeylonDemon\uninstall.exe` and registers it under
`HKLM\Software\Microsoft\Windows\CurrentVersion\Uninstall\CeylonDemonChessmaster`, so it appears
in Settings > Apps > Installed apps. Uninstalling removes the picture from the archive (and from a
VirtualStore copy), deletes the `.CMP` file, the registry entry and the install folder. Because a
running program cannot delete its own folder, `uninstall.exe` hands over to a copy in `%TEMP%`,
which waits for it to exit and is deleted at the next restart. If Chessmaster itself was removed
first, only CeylonDemon's own files and entry are removed.

## Adding another engine

The same steps work for any UCI engine: a folder with the engine, `polyglot.exe` and a
`polyglot.ini`; a `.CMP` file pointing at that `polyglot.exe`; and optionally a picture added
with `tools/cmfaces.py`. To build a one-click setup for another engine, change the constants and
texts at the top of `installer/setup.c` and `build_personality()`, and the payload files in
`installer/setup.rc`.
