// CeylonDemon for Chessmaster: one-click installer.
//
// Adds the CeylonDemon UCI chess engine to Chessmaster Grandmaster Edition,
// which only talks to WinBoard engines:
//   1. copies CeylonDemon, PolyGlot (the UCI -> WinBoard adapter) and
//      polyglot.ini to C:\Chess\CeylonDemon, and checks that they start;
//   2. adds the CeylonDemon portrait to Chessmaster's picture archive
//      (Data\TData\faces.idx and faces.dat);
//   3. writes the CeylonDemon personality (Data\Personalities\CeylonDemon.CMP).
// Running it again offers to reinstall or uninstall.
//
// Chessmaster is not UAC-aware, so Windows may redirect its writes to
// %LOCALAPPDATA%\VirtualStore. Copies there shadow the real files, so they are
// updated (faces) or removed (personality) as well.
//
// Command line, for testing: /quiet /install /uninstall /cmdir=<dir> /installdir=<dir>

#define WIN32_LEAN_AND_MEAN
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <cpuid.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

#include "resource.h"

#define APP_TITLE      L"CeylonDemon for Chessmaster"
#define DEFAULT_DIR    L"C:\\Chess\\CeylonDemon"
#define ENGINE_EXE     L"CeylonDemon-3.3.exe"
#define CMP_NAME       L"CeylonDemon.CMP"
#define FACE_NAME      "CeylonDemon.BMP"
#define RATING         3400
#define CMP_SIZE       3104
#define CM_REG_KEY     L"SOFTWARE\\Ubisoft\\Chessmaster Grandmaster Edition"
#define CM_DEFAULT_DIR L"Ubisoft\\Chessmaster Grandmaster Edition"
#define UNINSTALL_EXE  L"uninstall.exe"
#define PUBLISHER      L"Madushan Dissanayake"
#define REPO_URL       L"https://github.com/Madushan996/Chessmaster_Compatibility"

// The "Installed apps" entry. Test builds (no administrator rights) use a
// per-user entry with a different name.
#ifdef TEST_BUILD
#define UNINSTALL_ROOT HKEY_CURRENT_USER
#define UNINSTALL_KEY  L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\CeylonDemonChessmasterTest"
#else
#define UNINSTALL_ROOT HKEY_LOCAL_MACHINE
#define UNINSTALL_KEY  L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\CeylonDemonChessmaster"
#endif

#define PATH_LEN 1024

static wchar_t g_cmdir[PATH_LEN];
static BOOL g_cmdir_given;  // /cmdir= was passed (even empty): never search for another Chessmaster
static wchar_t g_instdir[PATH_LEN];
static wchar_t g_err[2048];
static BOOL g_quiet;

// ---- messages -----------------------------------------------------------------

static void print_utf8(const wchar_t *msg) {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h == NULL || h == INVALID_HANDLE_VALUE) return;
    char buf[8192];
    int n = WideCharToMultiByte(CP_UTF8, 0, msg, -1, buf, sizeof(buf) - 2, NULL, NULL);
    if (n <= 0) return;
    buf[n - 1] = '\n';
    DWORD written;
    WriteFile(h, buf, (DWORD)n, &written, NULL);
}

// Shows a message box (or prints it in /quiet mode, answering OK/Yes).
static int ask(UINT type, const wchar_t *fmt, ...) {
    wchar_t msg[4096];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(msg, 4095, fmt, ap);
    va_end(ap);
    msg[4095] = 0;
    if (g_quiet) {
        print_utf8(msg);
        UINT buttons = type & MB_TYPEMASK;
        if (buttons == MB_YESNO || buttons == MB_YESNOCANCEL) return IDYES;
        if (buttons == MB_RETRYCANCEL) return IDCANCEL;
        return IDOK;
    }
    return MessageBoxW(NULL, msg, APP_TITLE, type | MB_SETFOREGROUND);
}

static BOOL fail(const wchar_t *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(g_err, 2047, fmt, ap);
    va_end(ap);
    g_err[2047] = 0;
    return FALSE;
}

// Fails with the Windows error text for GetLastError().
static BOOL fail_win(const wchar_t *what, const wchar_t *path) {
    DWORD e = GetLastError();
    wchar_t sys[512] = L"";
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, e, 0,
                   sys, 511, NULL);
    return fail(L"%ls\n\n%ls\n\n%ls(error %lu)", what, path, sys, e);
}

// ---- files --------------------------------------------------------------------

static BOOL is_dir(const wchar_t *p) {
    DWORD a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static BOOL is_file(const wchar_t *p) {
    DWORD a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static void strip_slashes(wchar_t *p) {
    size_t n = wcslen(p);
    while (n > 3 && (p[n - 1] == L'\\' || p[n - 1] == L'/')) p[--n] = 0;
}

static BOOL read_all(const wchar_t *path, unsigned char **buf, DWORD *size) {
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return fail_win(L"Could not open a file:", path);
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart > 512 * 1024 * 1024) {
        CloseHandle(h);
        return fail(L"Unexpected file size:\n\n%ls", path);
    }
    *size = (DWORD)sz.QuadPart;
    *buf = malloc(*size ? *size : 1);
    DWORD got = 0;
    BOOL ok = *buf && ReadFile(h, *buf, *size, &got, NULL) && got == *size;
    if (!ok) fail_win(L"Could not read a file:", path);
    CloseHandle(h);
    if (!ok) { free(*buf); *buf = NULL; }
    return ok;
}

static BOOL write_raw(const wchar_t *path, const void *data, DWORD size) {
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return fail_win(L"Could not create a file:", path);
    const unsigned char *p = data;
    BOOL ok = TRUE;
    while (size > 0 && ok) {
        DWORD chunk = size > (1u << 20) ? (1u << 20) : size, wrote = 0;
        ok = WriteFile(h, p, chunk, &wrote, NULL) && wrote == chunk;
        p += chunk;
        size -= chunk;
    }
    if (ok) ok = FlushFileBuffers(h);
    if (!ok) fail_win(L"Could not write a file:", path);
    CloseHandle(h);
    if (!ok) DeleteFileW(path);
    return ok;
}

static BOOL replace_file(const wchar_t *tmp, const wchar_t *path) {
    if (MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return TRUE;
    fail_win(L"Could not replace a file:", path);
    DeleteFileW(tmp);
    return FALSE;
}

// Writes path.new, then swaps it in, so a failure never leaves a half-written file.
static BOOL write_file(const wchar_t *path, const void *data, DWORD size) {
    wchar_t tmp[PATH_LEN + 8];
    swprintf(tmp, PATH_LEN + 8, L"%ls.new", path);
    return write_raw(tmp, data, size) && replace_file(tmp, path);
}

static BOOL copy_if_missing(const wchar_t *from, const wchar_t *to) {
    if (is_file(to) || !is_file(from)) return TRUE;
    if (CopyFileW(from, to, TRUE)) return TRUE;
    return fail_win(L"Could not make a backup copy of:", from);
}

static void delete_tree(const wchar_t *dir) {
    wchar_t pattern[PATH_LEN + 4], path[PATH_LEN * 2];
    swprintf(pattern, PATH_LEN + 4, L"%ls\\*", dir);
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW(pattern, &fd);
    if (f != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
            swprintf(path, PATH_LEN * 2, L"%ls\\%ls", dir, fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) RemoveDirectoryW(path);
                else DeleteFileW(path);
            } else if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                delete_tree(path);
            } else {
                SetFileAttributesW(path, FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(path);
            }
        } while (FindNextFileW(f, &fd));
        FindClose(f);
    }
    RemoveDirectoryW(dir);
}

static BOOL load_payload(int id, const void **data, DWORD *size) {
    HRSRC r = FindResourceW(NULL, MAKEINTRESOURCEW(id), (LPCWSTR)RT_RCDATA);
    HGLOBAL g = r ? LoadResource(NULL, r) : NULL;
    *data = g ? LockResource(g) : NULL;
    *size = r ? SizeofResource(NULL, r) : 0;
    return *data != NULL && *size > 0;
}

static void put32(unsigned char *p, DWORD v) {
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

static DWORD get32(const unsigned char *p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((DWORD)p[3] << 24);
}

// ---- Chessmaster --------------------------------------------------------------

static BOOL valid_cmdir(const wchar_t *dir) {
    wchar_t p[PATH_LEN + 64];
    swprintf(p, PATH_LEN + 64, L"%ls\\Data\\Personalities", dir);
    if (!is_dir(p)) return FALSE;
    swprintf(p, PATH_LEN + 64, L"%ls\\Data\\TData\\faces.idx", dir);
    return is_file(p);
}

static BOOL try_cmdir(const wchar_t *dir) {
    wchar_t d[PATH_LEN];
    wcsncpy(d, dir, PATH_LEN - 1);
    d[PATH_LEN - 1] = 0;
    strip_slashes(d);
    if (!d[0] || !valid_cmdir(d)) return FALSE;
    wcscpy(g_cmdir, d);
    return TRUE;
}

// Finds Chessmaster from the registry, the default folder, or (if may_browse) by asking.
static BOOL find_cmdir(BOOL may_browse) {
    if (g_cmdir_given) {
        if (g_cmdir[0] && try_cmdir(g_cmdir)) return TRUE;
        fail(L"This is not a Chessmaster Grandmaster Edition folder:\n\n%ls", g_cmdir);
        g_cmdir[0] = 0;
        return FALSE;
    }
#ifdef TEST_BUILD
    return fail(L"Test build: pass /cmdir=<copy of Chessmaster>; it never uses the real one.");
#endif
    HKEY key;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, CM_REG_KEY, 0, KEY_READ | KEY_WOW64_32KEY, &key) ==
        ERROR_SUCCESS) {
        wchar_t buf[PATH_LEN] = L"";
        DWORD type, size = sizeof(buf) - sizeof(wchar_t);
        BOOL found = RegQueryValueExW(key, L"path", NULL, &type, (BYTE *)buf, &size) ==
                         ERROR_SUCCESS && type == REG_SZ;
        RegCloseKey(key);
        buf[PATH_LEN - 1] = 0;
        if (found && try_cmdir(buf)) return TRUE;
    }
    const wchar_t *roots[] = {L"%ProgramFiles(x86)%", L"%ProgramFiles%"};
    for (int i = 0; i < 2; i++) {
        wchar_t root[PATH_LEN], dir[PATH_LEN * 2];
        if (!ExpandEnvironmentStringsW(roots[i], root, PATH_LEN)) continue;
        swprintf(dir, PATH_LEN * 2, L"%ls\\%ls", root, CM_DEFAULT_DIR);
        if (try_cmdir(dir)) return TRUE;
    }
    if (g_quiet || !may_browse) return fail(L"Chessmaster Grandmaster Edition was not found.");
    ask(MB_OK | MB_ICONINFORMATION,
        L"Chessmaster Grandmaster Edition was not found in the usual place.\n\n"
        L"In the next window, please choose the folder where Chessmaster is installed "
        L"(the folder that contains Chessmaster.exe).");
    for (;;) {
        BROWSEINFOW bi = {0};
        bi.lpszTitle = L"Choose the Chessmaster Grandmaster Edition folder "
                       L"(the one that contains Chessmaster.exe):";
        bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
        PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
        if (!pidl) return fail(L"");
        wchar_t dir[MAX_PATH];
        BOOL got = SHGetPathFromIDListW(pidl, dir);
        CoTaskMemFree(pidl);
        if (got && try_cmdir(dir)) return TRUE;
        if (ask(MB_RETRYCANCEL | MB_ICONWARNING,
                L"That folder does not look like Chessmaster Grandmaster Edition "
                L"(it has no Data\\Personalities folder).\n\nClick Retry to choose again.") !=
            IDRETRY)
            return fail(L"");
    }
}

// %LOCALAPPDATA%\VirtualStore\<Chessmaster folder without the drive>
static BOOL virtualstore_dir(wchar_t *out, size_t n) {
    if (!(iswalpha(g_cmdir[0]) && g_cmdir[1] == L':' && g_cmdir[2] == L'\\')) return FALSE;
    PWSTR local = NULL;
    if (SHGetKnownFolderPath(&FOLDERID_LocalAppData, 0, NULL, &local) != S_OK) return FALSE;
    swprintf(out, n, L"%ls\\VirtualStore\\%ls", local, g_cmdir + 3);
    CoTaskMemFree(local);
    return TRUE;
}

static BOOL path_under(const wchar_t *path, const wchar_t *dir) {
    size_t n = wcslen(dir);
    return n > 0 && _wcsnicmp(path, dir, n) == 0 && (path[n] == L'\\' || path[n] == 0);
}

// Is Chessmaster (or the installed engine) running?
static BOOL programs_running(void) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return FALSE;
    PROCESSENTRY32W pe = {sizeof(pe)};
    BOOL running = FALSE;
    for (BOOL more = Process32FirstW(snap, &pe); more && !running; more = Process32NextW(snap, &pe)) {
        if (pe.th32ProcessID == GetCurrentProcessId()) continue;  // setup run from the install folder
        HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
        if (!p) continue;
        wchar_t image[PATH_LEN];
        DWORD len = PATH_LEN;
        if (QueryFullProcessImageNameW(p, 0, image, &len))
            running = path_under(image, g_cmdir) || path_under(image, g_instdir);
        CloseHandle(p);
    }
    CloseHandle(snap);
    return running;
}

// ---- picture archive (faces.idx / faces.dat) ----------------------------------
//
// faces.idx: 32-byte header (u32 32, "ChessmasterDat01", u32 count, u32 32, u32 0),
// then count 80-byte records sorted by name, ignoring case:
//   name[64], "PKZP", u32 offset, u32 size, u32 packed size.
// faces.dat: the same header, then the pictures, each compressed with
// PKWARE DCL "implode".

// A valid DCL stream that stores every byte as a literal: header (binary
// literals, 4 KB dictionary), then per byte a 0 bit and the 8 bits, then the
// end code (length 519: a 1 bit, length symbol 15 = seven 0 bits, 8 bits 255).
static unsigned char *implode_literal(const unsigned char *in, DWORD n, DWORD *out_n) {
    unsigned char *out = malloc(2 + (n * 9 + 16) / 8 + 4);
    if (!out) return NULL;
    DWORD o = 0, bits = 0;
    int count = 0;
    out[o++] = 0;
    out[o++] = 6;
#define PUT(v, nb)                       \
    do {                                 \
        bits |= (DWORD)(v) << count;     \
        count += (nb);                   \
        while (count >= 8) {             \
            out[o++] = (unsigned char)bits; \
            bits >>= 8;                  \
            count -= 8;                  \
        }                                \
    } while (0)
    for (DWORD i = 0; i < n; i++) {
        PUT(0, 1);
        PUT(in[i], 8);
    }
    PUT(1, 1);
    PUT(0, 7);
    PUT(255, 8);
#undef PUT
    if (count) out[o++] = (unsigned char)bits;
    *out_n = o;
    return out;
}

typedef struct {
    const char *name;
    const unsigned char *packed;
    DWORD size, packed_size;
} Face;

// Adds (bmp != NULL) or removes (bmp == NULL) FACE_NAME in the archive in dir.
static BOOL faces_update(const wchar_t *dir, const unsigned char *bmp, DWORD bmp_size) {
    wchar_t idx_path[PATH_LEN + 32], dat_path[PATH_LEN + 32];
    wchar_t idx_tmp[PATH_LEN + 40], dat_tmp[PATH_LEN + 40];
    swprintf(idx_path, PATH_LEN + 32, L"%ls\\faces.idx", dir);
    swprintf(dat_path, PATH_LEN + 32, L"%ls\\faces.dat", dir);
    swprintf(idx_tmp, PATH_LEN + 40, L"%ls.new", idx_path);
    swprintf(dat_tmp, PATH_LEN + 40, L"%ls.new", dat_path);

    unsigned char *idx = NULL, *dat = NULL, *packed = NULL, *new_idx = NULL, *new_dat = NULL;
    Face *faces = NULL;
    DWORD idx_size, dat_size, packed_size = 0;
    BOOL ok = FALSE;

    if (!read_all(idx_path, &idx, &idx_size) || !read_all(dat_path, &dat, &dat_size)) goto done;
    if (idx_size < 32 || dat_size < 32 || memcmp(idx + 4, "ChessmasterDat01", 16) ||
        memcmp(idx, dat, 20)) {
        fail(L"Chessmaster's picture archive is not in the expected format:\n\n%ls", idx_path);
        goto done;
    }
    DWORD count = get32(idx + 20);
    if (count > 100000 || idx_size != 32 + 80 * count) {
        fail(L"Chessmaster's picture archive is not in the expected format:\n\n%ls", idx_path);
        goto done;
    }
    faces = calloc(count + 1, sizeof(Face));
    if (!faces) goto done;
    DWORD n = 0;
    BOOL found = FALSE;
    for (DWORD k = 0; k < count; k++) {
        const unsigned char *rec = idx + 32 + 80 * k;
        DWORD off = get32(rec + 68), size = get32(rec + 72), psize = get32(rec + 76);
        if (!memchr(rec, 0, 64) || memcmp(rec + 64, "PKZP", 4) || off > dat_size ||
            psize > dat_size - off) {
            fail(L"Chessmaster's picture archive is damaged:\n\n%ls", idx_path);
            goto done;
        }
        if (_stricmp((const char *)rec, FACE_NAME) == 0) {
            found = TRUE;
            continue;
        }
        faces[n].name = (const char *)rec;
        faces[n].packed = dat + off;
        faces[n].size = size;
        faces[n].packed_size = psize;
        n++;
    }
    if (!bmp && !found) {
        ok = TRUE;  // nothing to remove
        goto done;
    }
    if (bmp) {
        packed = implode_literal(bmp, bmp_size, &packed_size);
        if (!packed) goto done;
        DWORD pos = 0;
        while (pos < n && _stricmp(faces[pos].name, FACE_NAME) < 0) pos++;
        memmove(faces + pos + 1, faces + pos, (n - pos) * sizeof(Face));
        faces[pos].name = FACE_NAME;
        faces[pos].packed = packed;
        faces[pos].size = bmp_size;
        faces[pos].packed_size = packed_size;
        n++;
    }

    DWORD new_idx_size = 32 + 80 * n, new_dat_size = 32;
    for (DWORD k = 0; k < n; k++) new_dat_size += faces[k].packed_size;
    new_idx = calloc(new_idx_size, 1);
    new_dat = malloc(new_dat_size);
    if (!new_idx || !new_dat) goto done;
    memcpy(new_idx, idx, 32);
    memcpy(new_dat, dat, 32);
    put32(new_idx + 20, n);
    put32(new_dat + 20, n);
    DWORD off = 32;
    for (DWORD k = 0; k < n; k++) {
        unsigned char *rec = new_idx + 32 + 80 * k;
        strncpy((char *)rec, faces[k].name, 63);
        memcpy(rec + 64, "PKZP", 4);
        put32(rec + 68, off);
        put32(rec + 72, faces[k].size);
        put32(rec + 76, faces[k].packed_size);
        memcpy(new_dat + off, faces[k].packed, faces[k].packed_size);
        off += faces[k].packed_size;
    }
    if (!write_raw(dat_tmp, new_dat, new_dat_size)) goto done;
    if (!write_raw(idx_tmp, new_idx, new_idx_size)) {
        DeleteFileW(dat_tmp);
        goto done;
    }
    ok = replace_file(dat_tmp, dat_path) && replace_file(idx_tmp, idx_path);

done:
    free(idx);
    free(dat);
    free(packed);
    free(new_idx);
    free(new_dat);
    free(faces);
    return ok;
}

// ---- personality (Data\Personalities\CeylonDemon.CMP) -------------------------
//
// The layout Chessmaster writes when it imports a WinBoard engine, plus the
// picture, rating (offset 0x38) and the descriptions shown in the
// personality window.

static void put_text(unsigned char *d, int off, int size, const char *text) {
    size_t len = strlen(text);
    if (len > (size_t)size - 1) len = size - 1;
    memset(d + off, 0, size);
    memcpy(d + off, text, len);
}

static BOOL build_personality(unsigned char d[CMP_SIZE]) {
    char path[PATH_LEN];
    wchar_t wpath[PATH_LEN + 16];
    swprintf(wpath, PATH_LEN + 16, L"%ls\\polyglot.exe", g_instdir);
    CharLowerW(wpath);
    int n = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, wpath, -1, path, sizeof(path), NULL, NULL);
    if (n <= 0 || n >= CMP_SIZE - 2582) return fail(L"The install folder path is too long:\n\n%ls", g_instdir);

    memset(d, 0, CMP_SIZE);
    memcpy(d, "Chessmaster: Grandmaster Edition", 32);
    put32(d + 0x24, 16);
    put32(d + 0x28, 178);
    put32(d + 0x38, RATING);
    put32(d + 0x4C, 99);
    put_text(d, 192, 30, "CMX.OBK");
    put_text(d, 452, 30, FACE_NAME);
    put_text(d, 482, 100, "Dual-frame NNUE engine from Sri Lanka");
    put_text(d, 582, 1000,
             "CeylonDemon is a UCI chess engine by Madushan Dissanayake, built on the codebase of "
             "his NARC engine. Its Resonance evaluator is a dual-frame NNUE: AEGIS judges the "
             "position from its own king's side (\"Am I safe?\") and LANCE from the enemy king's "
             "side (\"How much pressure is on them?\"). The network taught itself chess from 400 "
             "million positions of CeylonDemon's own self-play games, using no games or networks "
             "from other engines.");
    put_text(d, 1582, 1000,
             "CeylonDemon 3.3 plays at full strength (about 3450 in the author's 1 min + 1 sec "
             "tests). It runs in Chessmaster through the PolyGlot adapter, so Chessmaster's "
             "personality settings do not change its play.");
    put_text(d, 2582, CMP_SIZE - 2582, path);
    return TRUE;
}

// ---- engine -------------------------------------------------------------------

static void cpu_features(BOOL *avx2, BOOL *bmi2, BOOL *fast_pext) {
    unsigned a, b, c, d;
    *avx2 = *bmi2 = *fast_pext = FALSE;
    if (!__get_cpuid(0, &a, &b, &c, &d)) return;
    unsigned max_leaf = a;
    char vendor[13];
    memcpy(vendor, &b, 4);
    memcpy(vendor + 4, &d, 4);
    memcpy(vendor + 8, &c, 4);
    vendor[12] = 0;
    __get_cpuid(1, &a, &b, &c, &d);
    unsigned family = (a >> 8) & 15;
    if (family == 15) family += (a >> 20) & 255;
    BOOL ymm = FALSE;
    if ((c >> 27 & 1) && (c >> 28 & 1)) {  // OSXSAVE and AVX
        unsigned lo, hi;
        __asm__ volatile("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
        ymm = (lo & 6) == 6;
    }
    if (max_leaf >= 7) {
        __cpuid_count(7, 0, a, b, c, d);
        *avx2 = ymm && (b >> 5 & 1);
        *bmi2 = (b >> 8) & 1;
    }
    // PEXT is microcoded (slow) on AMD before Zen 3 (family 19h).
    *fast_pext = *bmi2 && (!strcmp(vendor, "GenuineIntel") ||
                           (!strcmp(vendor, "AuthenticAMD") && family >= 0x19));
}

static int physical_cores(void) {
    DWORD len = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, NULL, &len);
    unsigned char *buf = len ? malloc(len) : NULL;
    int cores = 0;
    if (buf && GetLogicalProcessorInformationEx(RelationProcessorCore,
                                                (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)buf, &len)) {
        for (DWORD off = 0; off < len;) {
            PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX p = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)(buf + off);
            cores++;
            off += p->Size;
        }
    }
    free(buf);
    if (cores < 1) {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        cores = (int)si.dwNumberOfProcessors;
    }
    return cores < 1 ? 1 : cores;
}

static BOOL write_ini(void) {
    int threads = physical_cores();
    if (threads > 8) threads = 8;
    MEMORYSTATUSEX ms = {sizeof(ms)};
    int hash = GlobalMemoryStatusEx(&ms) && ms.ullTotalPhys < 4ull << 30 ? 64 : 128;
    wchar_t text[4096];
    swprintf(text, 4096,
             L"; Settings for CeylonDemon in Chessmaster (PolyGlot adapter).\r\n"
             L"; Hash = memory in MB, Threads = processor cores CeylonDemon may use.\r\n"
             L"\r\n"
             L"[PolyGlot]\r\n"
             L"EngineName    = CeylonDemon 3.3\r\n"
             L"EngineDir     = %ls\r\n"
             L"EngineCommand = %ls\r\n"
             L"Book          = false\r\n"
             L"Resign        = false\r\n"
             L"Log           = false\r\n"
             L"LogFile       = %ls\\polyglot.log\r\n"
             L"\r\n"
             L"[Engine]\r\n"
             L"Hash          = %d\r\n"
             L"Threads       = %d\r\n"
             L"Move Overhead = 50\r\n",
             g_instdir, ENGINE_EXE, g_instdir, hash, threads);
    char ansi[8192];
    int n = WideCharToMultiByte(CP_ACP, 0, text, -1, ansi, sizeof(ansi), NULL, NULL);
    if (n <= 0) return fail(L"Could not write the engine settings.");
    wchar_t path[PATH_LEN + 32];
    swprintf(path, PATH_LEN + 32, L"%ls\\polyglot.ini", g_instdir);
    return write_file(path, ansi, (DWORD)(n - 1));
}

// Starts PolyGlot the way Chessmaster does and checks that CeylonDemon plays a move.
static BOOL test_engine(void) {
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
    HANDLE in_r, in_w, out_r, out_w;
    if (!CreatePipe(&out_r, &out_w, &sa, 0)) return fail(L"Could not start the engine test.");
    if (!CreatePipe(&in_r, &in_w, &sa, 0)) {
        CloseHandle(out_r);
        CloseHandle(out_w);
        return fail(L"Could not start the engine test.");
    }
    SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(in_w, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = {sizeof(si)};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = in_r;
    si.hStdOutput = out_w;
    si.hStdError = out_w;
    PROCESS_INFORMATION pi;
    wchar_t cmd[PATH_LEN + 64];
    swprintf(cmd, PATH_LEN + 64, L"\"%ls\\polyglot.exe\" -t1", g_instdir);
    BOOL started = CreateProcessW(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, g_instdir,
                                  &si, &pi);
    CloseHandle(in_r);
    CloseHandle(out_w);
    if (!started) {
        CloseHandle(in_w);
        CloseHandle(out_r);
        return fail_win(L"Could not start PolyGlot:", cmd);
    }

    static char out[65536];
    DWORD got_total = 0, wrote;
    const char *hello = "xboard\nprotover 2\n";
    const char *play = "new\nsd 6\ngo\n";
    WriteFile(in_w, hello, (DWORD)strlen(hello), &wrote, NULL);
    BOOL ready = FALSE, moved = FALSE;
    for (DWORD start = GetTickCount(); GetTickCount() - start < 30000 && !moved;) {
        DWORD avail = 0;
        if (!PeekNamedPipe(out_r, NULL, 0, NULL, &avail, NULL)) break;  // PolyGlot exited
        if (avail == 0) {
            Sleep(50);
            continue;
        }
        if (got_total + avail >= sizeof(out) - 1) got_total = 0;  // keep only recent output
        DWORD got = 0;
        if (!ReadFile(out_r, out + got_total, avail, &got, NULL)) break;
        got_total += got;
        out[got_total] = 0;
        if (!ready && strstr(out, "feature done=1")) {
            ready = TRUE;
            got_total = 0;
            WriteFile(in_w, play, (DWORD)strlen(play), &wrote, NULL);
        } else if (ready && strstr(out, "move ")) {
            moved = TRUE;
        }
    }
    WriteFile(in_w, "quit\n", 5, &wrote, NULL);
    CloseHandle(in_w);
    if (WaitForSingleObject(pi.hProcess, 5000) != WAIT_OBJECT_0) TerminateProcess(pi.hProcess, 1);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(out_r);
    if (moved) return TRUE;
    return fail(L"CeylonDemon was copied to %ls but it did not start.\n\n"
                L"This is usually caused by antivirus software (such as Windows Security) "
                L"blocking the engine. Allow CeylonDemon in your antivirus, then run this "
                L"setup again.",
                g_instdir);
}

// ---- Windows "Installed apps" entry -------------------------------------------

static void set_sz(HKEY key, const wchar_t *name, const wchar_t *value) {
    RegSetValueExW(key, name, 0, REG_SZ, (const BYTE *)value,
                   (DWORD)((wcslen(value) + 1) * sizeof(wchar_t)));
}

static void set_dword(HKEY key, const wchar_t *name, DWORD value) {
    RegSetValueExW(key, name, 0, REG_DWORD, (const BYTE *)&value, sizeof(value));
}

// Keeps a copy of this setup as uninstall.exe and lists CeylonDemon in
// Settings > Apps > Installed apps, so it can be removed like any program.
static BOOL register_uninstall(DWORD installed_bytes) {
    wchar_t self[PATH_LEN], exe[PATH_LEN + 32], cmd[PATH_LEN * 4], icon[PATH_LEN + 40];
    swprintf(exe, PATH_LEN + 32, L"%ls\\%ls", g_instdir, UNINSTALL_EXE);
    if (!GetModuleFileNameW(NULL, self, PATH_LEN)) return fail(L"Could not find the setup file.");
    if (_wcsicmp(self, exe) && !CopyFileW(self, exe, FALSE))
        return fail_win(L"Could not copy the uninstaller to:", exe);

    HKEY key;
    LONG r = RegCreateKeyExW(UNINSTALL_ROOT, UNINSTALL_KEY, 0, NULL, 0, KEY_WRITE, NULL, &key, NULL);
    if (r != ERROR_SUCCESS) {
        SetLastError((DWORD)r);
        return fail_win(L"Could not add CeylonDemon to the list of installed apps:", UNINSTALL_KEY);
    }
    swprintf(cmd, PATH_LEN * 4, L"\"%ls\" /uninstall \"/installdir=%ls\" \"/cmdir=%ls\"", exe, g_instdir,
             g_cmdir);
    swprintf(icon, PATH_LEN + 40, L"%ls,0", exe);
    set_sz(key, L"DisplayName", APP_TITLE);
    set_sz(key, L"DisplayVersion", L"3.3");
    set_sz(key, L"Publisher", PUBLISHER);
    set_sz(key, L"DisplayIcon", icon);
    set_sz(key, L"UninstallString", cmd);
    set_sz(key, L"InstallLocation", g_instdir);
    set_sz(key, L"URLInfoAbout", REPO_URL);
    set_sz(key, L"HelpLink", REPO_URL);
    set_dword(key, L"NoModify", 1);
    set_dword(key, L"NoRepair", 1);
    set_dword(key, L"EstimatedSize", installed_bytes / 1024 * 2);  // KB, files + uninstall.exe
    RegCloseKey(key);
    return TRUE;
}

// ---- install / uninstall ------------------------------------------------------

static BOOL install(void) {
    BOOL avx2, bmi2, fast_pext;
    cpu_features(&avx2, &bmi2, &fast_pext);
    const void *eng_bmi2 = NULL, *eng_avx2 = NULL, *polyglot, *portrait, *engine;
    DWORD bmi2_size = 0, avx2_size = 0, polyglot_size, portrait_size, engine_size;
    BOOL have_bmi2 = load_payload(IDR_ENGINE_BMI2, &eng_bmi2, &bmi2_size);
    BOOL have_avx2 = load_payload(IDR_ENGINE_AVX2, &eng_avx2, &avx2_size);
    if (!load_payload(IDR_POLYGLOT, &polyglot, &polyglot_size) ||
        !load_payload(IDR_PORTRAIT, &portrait, &portrait_size))
        return fail(L"This setup file is damaged. Please download it again.");
    if (!avx2)
        return fail(L"Sorry, CeylonDemon cannot run on this computer.\n\n"
                    L"It needs a processor with AVX2: Intel Core from 2013 (Haswell) or newer, "
                    L"or AMD from 2015 or newer.");
    if (have_avx2 && (!fast_pext || !have_bmi2)) {
        engine = eng_avx2;
        engine_size = avx2_size;
    } else if (have_bmi2 && bmi2) {
        engine = eng_bmi2;
        engine_size = bmi2_size;
    } else {
        return fail(L"Sorry, CeylonDemon cannot run on this computer: its processor does not "
                    L"support the BMI2 instructions this version of CeylonDemon needs.");
    }

    int r = SHCreateDirectoryExW(NULL, g_instdir, NULL);
    if (r != ERROR_SUCCESS && r != ERROR_ALREADY_EXISTS && r != ERROR_FILE_EXISTS)
        return fail(L"Could not create the folder:\n\n%ls", g_instdir);

    wchar_t path[PATH_LEN + 64];
    swprintf(path, PATH_LEN + 64, L"%ls\\%ls", g_instdir, ENGINE_EXE);
    if (!write_file(path, engine, engine_size)) return FALSE;
    swprintf(path, PATH_LEN + 64, L"%ls\\polyglot.exe", g_instdir);
    if (!write_file(path, polyglot, polyglot_size)) return FALSE;
    swprintf(path, PATH_LEN + 64, L"%ls\\CeylonDemon.bmp", g_instdir);
    if (!write_file(path, portrait, portrait_size)) return FALSE;
    if (!write_ini() || !test_engine()) return FALSE;

    // Portrait, in the real archive and in any VirtualStore copy that shadows it.
    wchar_t tdata[PATH_LEN + 32], vs[PATH_LEN * 2], vs_tdata[PATH_LEN * 2 + 32];
    wchar_t backup[PATH_LEN + 64], from[PATH_LEN * 2 + 64];
    swprintf(tdata, PATH_LEN + 32, L"%ls\\Data\\TData", g_cmdir);
    BOOL have_vs = virtualstore_dir(vs, PATH_LEN * 2);
    swprintf(vs_tdata, PATH_LEN * 2 + 32, L"%ls\\Data\\TData", have_vs ? vs : L"");
    BOOL vs_faces = FALSE;
    if (have_vs) {
        swprintf(from, PATH_LEN * 2 + 64, L"%ls\\faces.idx", vs_tdata);
        vs_faces = is_file(from);
    }

    swprintf(backup, PATH_LEN + 64, L"%ls\\backup", g_instdir);
    CreateDirectoryW(backup, NULL);
    const wchar_t *names[] = {L"faces.idx", L"faces.dat"};
    for (int i = 0; i < 2; i++) {
        swprintf(from, PATH_LEN * 2 + 64, L"%ls\\%ls", tdata, names[i]);
        swprintf(path, PATH_LEN + 64, L"%ls\\%ls", backup, names[i]);
        if (!copy_if_missing(from, path)) return FALSE;
    }
    if (!faces_update(tdata, portrait, portrait_size)) return FALSE;
    if (vs_faces && !faces_update(vs_tdata, portrait, portrait_size)) return FALSE;

    // Personality.
    unsigned char cmp[CMP_SIZE];
    if (!build_personality(cmp)) return FALSE;
    swprintf(path, PATH_LEN + 64, L"%ls\\Data\\Personalities\\%ls", g_cmdir, CMP_NAME);
    if (!write_file(path, cmp, CMP_SIZE)) return FALSE;
    if (have_vs) {
        swprintf(from, PATH_LEN * 2 + 64, L"%ls\\Data\\Personalities\\%ls", vs, CMP_NAME);
        DeleteFileW(from);
    }
    return register_uninstall(engine_size + polyglot_size);
}

static BOOL uninstall(BOOL have_cm) {
    wchar_t path[PATH_LEN * 2 + 64], vs[PATH_LEN * 2];
    BOOL have_vs = have_cm && virtualstore_dir(vs, PATH_LEN * 2);

    if (!have_cm) goto files;  // Chessmaster is gone: only remove CeylonDemon's own files
    swprintf(path, PATH_LEN * 2 + 64, L"%ls\\Data\\TData", g_cmdir);
    if (!faces_update(path, NULL, 0)) return FALSE;
    if (have_vs) {
        swprintf(path, PATH_LEN * 2 + 64, L"%ls\\Data\\TData\\faces.idx", vs);
        if (is_file(path)) {
            swprintf(path, PATH_LEN * 2 + 64, L"%ls\\Data\\TData", vs);
            if (!faces_update(path, NULL, 0)) return FALSE;
        }
    }

    swprintf(path, PATH_LEN * 2 + 64, L"%ls\\Data\\Personalities\\%ls", g_cmdir, CMP_NAME);
    if (is_file(path) && !DeleteFileW(path)) return fail_win(L"Could not delete:", path);
    if (have_vs) {
        swprintf(path, PATH_LEN * 2 + 64, L"%ls\\Data\\Personalities\\%ls", vs, CMP_NAME);
        DeleteFileW(path);
    }

files:
    RegDeleteKeyW(UNINSTALL_ROOT, UNINSTALL_KEY);
    if (is_dir(g_instdir)) delete_tree(g_instdir);
    if (!_wcsicmp(g_instdir, DEFAULT_DIR)) RemoveDirectoryW(L"C:\\Chess");  // only if empty
    return TRUE;
}

// Uninstalling from inside the install folder would leave the running
// uninstall.exe (and so the folder) behind: run a copy from %TEMP% instead,
// which waits for this process to exit.
static BOOL relaunch_from_temp(void) {
    wchar_t self[PATH_LEN], tmp[PATH_LEN], copy[PATH_LEN + 64], cmd[PATH_LEN * 4];
    if (!GetModuleFileNameW(NULL, self, PATH_LEN) || !GetTempPathW(PATH_LEN, tmp))
        return fail(L"Could not start the uninstaller.");
    swprintf(copy, PATH_LEN + 64, L"%lsCeylonDemon-uninstall.exe", tmp);
    if (!CopyFileW(self, copy, FALSE)) return fail_win(L"Could not start the uninstaller:", copy);
    swprintf(cmd, PATH_LEN * 4, L"\"%ls\" /uninstall /confirmed%ls /waitpid=%lu \"/installdir=%ls\" \"/cmdir=%ls\"",
             copy, g_quiet ? L" /quiet" : L"", GetCurrentProcessId(), g_instdir, g_cmdir);
    STARTUPINFOW si = {sizeof(si)};
    PROCESS_INFORMATION pi;
    if (!CreateProcessW(copy, cmd, NULL, NULL, FALSE, 0, NULL, tmp, &si, &pi))
        return fail_win(L"Could not start the uninstaller:", copy);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return TRUE;
}

static BOOL installed(void) {
    wchar_t path[PATH_LEN + 64];
    swprintf(path, PATH_LEN + 64, L"%ls\\Data\\Personalities\\%ls", g_cmdir, CMP_NAME);
    if (is_file(path)) return TRUE;
    swprintf(path, PATH_LEN + 64, L"%ls\\polyglot.exe", g_instdir);
    return is_file(path);
}

// ---- main ---------------------------------------------------------------------

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmdline, int show) {
    (void)inst;
    (void)prev;
    (void)cmdline;
    (void)show;
    enum { ASK, INSTALL, UNINSTALL } mode = ASK;
    BOOL confirmed = FALSE;
    DWORD wait_pid = 0;
    wcscpy(g_instdir, DEFAULT_DIR);

    int argc;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv && i < argc; i++) {
        if (!_wcsicmp(argv[i], L"/quiet")) g_quiet = TRUE;
        else if (!_wcsicmp(argv[i], L"/install")) mode = INSTALL;
        else if (!_wcsicmp(argv[i], L"/uninstall")) mode = UNINSTALL;
        else if (!_wcsicmp(argv[i], L"/confirmed")) confirmed = TRUE;
        else if (!_wcsnicmp(argv[i], L"/waitpid=", 9)) wait_pid = wcstoul(argv[i] + 9, NULL, 10);
        else if (!_wcsnicmp(argv[i], L"/cmdir=", 7)) {
            wcsncpy(g_cmdir, argv[i] + 7, PATH_LEN - 1);
            g_cmdir_given = TRUE;
        }
        else if (!_wcsnicmp(argv[i], L"/installdir=", 12)) {
            wcsncpy(g_instdir, argv[i] + 12, PATH_LEN - 1);
            strip_slashes(g_instdir);
        }
    }
    if (argv) LocalFree(argv);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    if (wait_pid) {  // started by relaunch_from_temp(): let the uninstall.exe we came from exit
        HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, wait_pid);
        if (parent) {
            WaitForSingleObject(parent, 15000);
            CloseHandle(parent);
        }
    }

    // Removing CeylonDemon must work even if Chessmaster itself is already gone.
    BOOL have_cm = find_cmdir(mode != UNINSTALL);
    if (!have_cm && mode != UNINSTALL) {
        if (g_err[0]) ask(MB_OK | MB_ICONERROR, L"%ls", g_err);
        return 1;
    }
    while (programs_running()) {
        if (ask(MB_RETRYCANCEL | MB_ICONWARNING,
                L"Chessmaster is open.\n\nPlease close Chessmaster, then click Retry.") != IDRETRY)
            return 1;
    }

    if (mode == UNINSTALL && !confirmed) {
        if (ask(MB_OKCANCEL | MB_ICONQUESTION,
                L"Remove CeylonDemon from Chessmaster?\n\n"
                L"Chessmaster will be put back as it was before CeylonDemon was added.") != IDOK)
            return 0;
    } else if (mode == ASK && installed()) {
        int r = ask(MB_YESNOCANCEL | MB_ICONQUESTION,
                    L"CeylonDemon is already installed in Chessmaster.\n\n"
                    L"Yes:  install it again (repair)\n"
                    L"No:  remove CeylonDemon from Chessmaster\n"
                    L"Cancel:  do nothing");
        if (r == IDCANCEL) return 0;
        mode = r == IDYES ? INSTALL : UNINSTALL;
    } else if (mode == ASK) {
        if (ask(MB_OKCANCEL | MB_ICONINFORMATION,
                L"This adds the CeylonDemon 3.3 chess engine to Chessmaster Grandmaster "
                L"Edition, with its own picture and a 3400 rating.\n\n"
                L"Chessmaster folder:\n%ls\n\nCeylonDemon will be installed in:\n%ls\n\n"
                L"Click OK to install.",
                g_cmdir, g_instdir) != IDOK)
            return 0;
        mode = INSTALL;
    }

    wchar_t self[PATH_LEN] = L"";
    GetModuleFileNameW(NULL, self, PATH_LEN);
    if (mode == UNINSTALL && path_under(self, g_instdir)) {
        if (relaunch_from_temp()) return 0;
        ask(MB_OK | MB_ICONERROR, L"Sorry, something went wrong.\n\n%ls", g_err);
        return 1;
    }

    HCURSOR old = SetCursor(LoadCursor(NULL, IDC_WAIT));
    BOOL ok = mode == INSTALL ? install() : uninstall(have_cm);
    SetCursor(old);
    if (wait_pid) MoveFileExW(self, NULL, MOVEFILE_DELAY_UNTIL_REBOOT);  // the %TEMP% copy
    if (!ok) {
        ask(MB_OK | MB_ICONERROR, L"Sorry, something went wrong.\n\n%ls", g_err);
        return 1;
    }
    if (mode == INSTALL)
        ask(MB_OK | MB_ICONINFORMATION,
            L"CeylonDemon is ready!\n\n"
            L"Open Chessmaster and choose CeylonDemon as a player. You will find it in the "
            L"list of opponents, in the \"Xboard Engines\" group.\n\n"
            L"To remove it later: Windows Settings > Apps > Installed apps > "
            L"CeylonDemon for Chessmaster > Uninstall.");
    else
        ask(MB_OK | MB_ICONINFORMATION, L"CeylonDemon has been removed from Chessmaster.");
    return 0;
}
