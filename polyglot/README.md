# PolyGlot for Chessmaster

PolyGlot 1.4.70b, the UCI-to-WinBoard adapter by Fabien Letouzey, later versions by H.G. Muller,
with the changes Chessmaster Grandmaster Edition needs. Licence: GNU GPL version 2 or later
(see `src/COPYING`).

- **Upstream:** `https://hgm.nubati.net/git/polyglot.git`, commit
  `5904a294b3b33dfd3ca11e4e48133151787f0d8f` ("Add forgotten files 1.4.70b").
- **`src/`:** the C sources of that commit with [`chessmaster.patch`](chessmaster.patch) applied.
  Only the C version is included (upstream also has an older C++ version).

## Changes

1. **Ignore unknown command-line arguments** (`main.c`). Chessmaster starts engines with `-t1`;
   upstream PolyGlot exits with `Incorrect use of option: "-t1"`, so the engine never plays.
2. **Find `polyglot.ini` next to `polyglot.exe`** (`main.c`, Windows only) when it is not in the
   current folder.
3. **64-bit handle cast** (`pipex_win32.c`): `(long)` becomes `(intptr_t)`.

## Building

With MSYS2 UCRT64 gcc (GCC 15). The extra flags let the 2009-era C build with a modern
compiler and Windows headers:

```sh
gcc -O2 -DNDEBUG -std=gnu17 -fcommon -include windows.h -include direct.h -c src/*.c
gcc -s -static *.o -o polyglot.exe
```

- `-std=gnu17`: `bool` is a keyword in C23, the GCC 15 default.
- `-fcommon`: some globals are defined in headers.
- `-include windows.h`: newer `winnt.h` declares ARM64 register fields named `D8`... `D15`,
  which clash with PolyGlot's square macros unless the Windows header comes first.
- `-include direct.h`: declares `_chdir`.

`build.ps1` in the repository root does this for the installer.
