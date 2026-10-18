<div align="center">

<img src="assets\Screenshot.png" alt="CeylonDemoninCM" width="220">

# Play CeylonDemon in Chessmaster

Add the **CeylonDemon 3.3** chess engine to **Chessmaster Grandmaster Edition**
with one download and a few clicks, with no technical knowledge needed.

</div>

CeylonDemon then appears in Chessmaster's list of opponents, with its own picture and a rating of
3400, and you can play against it or watch it play Chessmaster's own characters.

## What you need

- **Chessmaster Grandmaster Edition**, already installed.
- **Windows 10 or 11**.
- A computer made in **2013 or later** (CeylonDemon needs a modern processor).

## Install

1. **Close Chessmaster** if it is open.
2. Download **`CeylonDemon-Chessmaster-Setup.exe`** from the
   [Releases page](https://github.com/Madushan996/Chessmaster_Compatibility/releases/latest).
3. Double-click the downloaded file.
   - If a blue window says **"Windows protected your PC"**, click **More info**, then **Run anyway**.
     ([Why does this happen?](#windows-says-windows-protected-your-pc))
   - When Windows asks **"Do you want to allow this app to make changes to your device?"**, click **Yes**.
4. Click **OK** to install. After a few seconds you will see **"CeylonDemon is ready!"**

That's all. You can delete the downloaded file now, or keep it for removing CeylonDemon later.

## Play against CeylonDemon

1. Open Chessmaster.
2. Choose your opponent as you normally would. **CeylonDemon** is in the list of players, in the
   **"Xboard Engines"** group.
3. Start the game.

Good to know:

- The first few moves come from Chessmaster's opening book, so they appear instantly. After that,
  CeylonDemon thinks for itself.
- CeylonDemon always plays at **full strength**. Chessmaster's personality settings (style,
  strength and so on) do not change how it plays, and its rating cannot be edited.
- Chessmaster recommends not playing **Rated Games** against added engines, because it cannot
  judge their strength on your computer, and your own rating could become less accurate.

## Remove CeylonDemon

Remove it like any other program:

1. Open the **Start** menu and click **Settings** (the gear icon).
2. Click **Apps**, then **Installed apps** (on Windows 10: **Apps & features**).
3. Find **CeylonDemon for Chessmaster**, click the **⋯** button next to it (on Windows 10, click
   the name), then click **Uninstall**.
4. Click **Yes** when Windows asks for permission, then **OK** to confirm.

Chessmaster is put back exactly as it was before CeylonDemon was added.

You can also run `CeylonDemon-Chessmaster-Setup.exe` again: it will say CeylonDemon is already
installed. Click **No** to remove it, or **Yes** to install it again, which also repairs a broken
installation.

## Questions and problems

### Windows says "Windows protected your PC"

Windows shows this for programs that are new or not yet downloaded by many people. Click
**More info**, then **Run anyway**. The setup and everything in it are open source: the code is in
this repository.

### My antivirus blocked the setup or CeylonDemon

Antivirus programs sometimes wrongly flag new chess engines, because they are unknown programs
that use the processor heavily. If this happens, allow the setup file and the folder
`C:\Chess\CeylonDemon` in your antivirus, then run the setup again.

### The setup says it can't find Chessmaster

It will ask you to choose the Chessmaster folder. Pick the folder that contains
`Chessmaster.exe`, usually
`C:\Program Files (x86)\Ubisoft\Chessmaster Grandmaster Edition`.

### The setup says "Chessmaster is open"

Close Chessmaster completely, then click **Retry**.

### CeylonDemon's clock runs but it doesn't move

Run the setup again and click **Yes** to repair. If it still doesn't move, your antivirus is
probably blocking it (see above).

### Does this work with other Chessmaster versions?

It has only been tested with **Chessmaster Grandmaster Edition** on Windows 11.

### What exactly does the setup change?

- It creates the folder `C:\Chess\CeylonDemon` with CeylonDemon, PolyGlot (a small helper
  program that lets Chessmaster talk to CeylonDemon), their settings, the uninstaller, and a
  backup of Chessmaster's picture files.
- It adds **CeylonDemon for Chessmaster** to Windows' list of installed apps, so you can remove it
  from Settings.
- It adds CeylonDemon's picture to Chessmaster's picture files (`Data\TData\faces.idx` and
  `faces.dat`).
- It adds the CeylonDemon player file (`Data\Personalities\CeylonDemon.CMP`).

Removing CeylonDemon undoes all of this. Nothing else on your computer is changed.

## For developers

- [How it works](docs/how-it-works.md): why Chessmaster needs PolyGlot, the fix it needed, and
  the formats of Chessmaster's player and picture files. Use it to add other engines the same way.
- [`installer/setup.c`](installer/setup.c): the setup program.
- [`polyglot/`](polyglot): PolyGlot source with the Chessmaster changes.
- [`tools/cmfaces.py`](tools/cmfaces.py): read and edit Chessmaster's picture archive.

Building needs [MSYS2](https://www.msys2.org) with the UCRT64 gcc
(`pacman -S mingw-w64-ucrt-x86_64-gcc`) and the CeylonDemon 3.3 release files:

```powershell
.\build.ps1 -EngineDir <folder with CeylonDemon-3.3-x86-64-bmi2.exe>
```

The script checks the engine files against the official 3.3 checksums and writes
`dist\CeylonDemon-Chessmaster-Setup.exe`. `-Test` builds a version that runs without administrator
rights, for testing against copied folders:
`setup.exe /quiet /install|/uninstall /cmdir=<copy of Chessmaster> /installdir=<folder>`.

## Credits and licences

- **CeylonDemon** by Madushan Dissanayake: GPL-3.0. Source code:
  [github.com/Madushan996/CeylonDemon](https://github.com/Madushan996/CeylonDemon).
- **PolyGlot** by Fabien Letouzey, later versions by H.G. Muller: GPL-2.0-or-later. The modified
  source is in [`polyglot/`](polyglot).
- This repository: GPL-3.0, see [LICENSE](LICENSE).

Chessmaster is a trademark of Ubisoft. This project is not affiliated with or endorsed by Ubisoft,
and it contains no Ubisoft files: the setup edits the copy of Chessmaster already on your
computer.
