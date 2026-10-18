"""Read and edit Chessmaster's picture archive (Data/TData/faces.idx + faces.dat).

Usage:
  cmfaces.py list    <faces.idx> <faces.dat>
  cmfaces.py check   <faces.idx> <faces.dat>             decode every picture
  cmfaces.py extract <faces.idx> <faces.dat> <name> <out.bmp>
  cmfaces.py put     <faces.idx> <faces.dat> <in.bmp> <name>...   add or replace
  cmfaces.py remove  <faces.idx> <faces.dat> <name>...

Pictures are 76 x 97, 24-bit BMP files. Back up both files before changing them.

idx: 32-byte header (u32 32, "ChessmasterDat01", u32 count, u32 32, u32 0), then
count x 80-byte records: name[64], "PKZP", u32 offset, u32 size, u32 packed_size.
dat: same 32-byte header, then entries compressed with PKWARE DCL implode.
"""
import struct, sys

# ---- PKWARE DCL (blast) -------------------------------------------------------

LENLEN = [2, 35, 36, 53, 38, 23]
DISTLEN = [2, 20, 53, 230, 247, 151, 248]
BASE = [3, 2, 4, 5, 6, 7, 8, 9, 10, 12, 16, 24, 40, 72, 136, 264]
EXTRA = [0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8]


def expand(rep):
    lengths = []
    for b in rep:
        lengths += [b & 15] * ((b >> 4) + 1)
    return lengths


def canonical(lengths):
    """symbol -> (code, length) for a canonical code (shorter codes first)."""
    codes, code = {}, 0
    for ln in range(1, 14):
        for sym, l in enumerate(lengths):
            if l == ln:
                codes[sym] = (code, ln)
                code += 1
        code <<= 1
    return codes


LENCODE = canonical(expand(LENLEN))
DISTCODE = canonical(expand(DISTLEN))


class BitReader:
    def __init__(self, data):
        self.data, self.pos, self.buf, self.cnt = data, 0, 0, 0

    def bits(self, n):
        while self.cnt < n:
            self.buf |= self.data[self.pos] << self.cnt
            self.pos += 1
            self.cnt += 8
        v = self.buf & ((1 << n) - 1)
        self.buf >>= n
        self.cnt -= n
        return v

    def decode(self, table):
        rev = {v: k for k, v in table.items()}
        code, ln = 0, 0
        while True:
            code = (code << 1) | (self.bits(1) ^ 1)  # codes are stored inverted
            ln += 1
            if (code, ln) in rev:
                return rev[(code, ln)]
            if ln > 13:
                raise ValueError('bad code')


def explode(data):
    r = BitReader(data)
    lit, dictbits = r.bits(8), r.bits(8)
    if lit != 0 or dictbits not in (4, 5, 6):
        raise ValueError(f'unsupported header {lit} {dictbits}')
    out = bytearray()
    while True:
        if r.bits(1):
            sym = r.decode(LENCODE)
            ln = BASE[sym] + r.bits(EXTRA[sym])
            if ln == 519:
                return bytes(out)
            s = 2 if ln == 2 else dictbits
            dist = (r.decode(DISTCODE) << s) + r.bits(s) + 1
            for _ in range(ln):
                out.append(out[-dist])
        else:
            out.append(r.bits(8))


def implode(data):
    """Literal-only DCL stream (valid, just not compressed)."""
    out, buf, cnt = bytearray([0, 6]), 0, 0

    def put(v, n):
        nonlocal buf, cnt
        buf |= v << cnt
        cnt += n
        while cnt >= 8:
            out.append(buf & 255)
            buf >>= 8
            cnt -= 8

    for b in data:
        put(0, 1)
        put(b, 8)
    put(1, 1)  # end marker: length 519 = symbol 15 + 255
    code, ln = LENCODE[15]
    for i in range(ln - 1, -1, -1):
        put(((code >> i) & 1) ^ 1, 1)
    put(255, 8)
    if cnt:
        out.append(buf & 255)
    return bytes(out)


# ---- archive ------------------------------------------------------------------

def read_archive(idx_path, dat_path):
    idx = open(idx_path, 'rb').read()
    dat = open(dat_path, 'rb').read()
    count = struct.unpack('<I', idx[20:24])[0]
    entries = []
    for k in range(count):
        r = idx[32 + 80 * k: 112 + 80 * k]
        name = r[:64].split(b'\0')[0].decode('latin-1')
        off, size, packed = struct.unpack('<III', r[68:80])
        entries.append((name, dat[off:off + packed], size))
    return idx[:32], dat[:32], entries


def write_archive(idx_path, dat_path, idx_head, dat_head, entries):
    idx = bytearray(idx_head)
    idx[20:24] = struct.pack('<I', len(entries))
    dat = bytearray(dat_head)
    dat[20:24] = struct.pack('<I', len(entries))
    for name, packed, size in entries:
        rec = name.encode('latin-1').ljust(64, b'\0') + b'PKZP'
        rec += struct.pack('<III', len(dat), size, len(packed))
        idx += rec
        dat += packed
    open(idx_path, 'wb').write(idx)
    open(dat_path, 'wb').write(dat)


if __name__ == '__main__':
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    cmd = sys.argv[1]
    if cmd == 'list':
        _, _, entries = read_archive(sys.argv[2], sys.argv[3])
        for name, packed, size in entries:
            print(f'{name:40} {size:7} bytes')
    elif cmd == 'remove':
        ih, dh, entries = read_archive(sys.argv[2], sys.argv[3])
        drop = {n.lower() for n in sys.argv[4:]}
        entries = [e for e in entries if e[0].lower() not in drop]
        write_archive(sys.argv[2], sys.argv[3], ih, dh, entries)
        print('wrote', len(entries), 'entries')
    elif cmd == 'check':  # decode every entry, round-trip one through implode
        ih, dh, entries = read_archive(sys.argv[2], sys.argv[3])
        print('header idx', ih[20:24] == dh[20:24], ih == dh)
        for name, packed, size in entries:
            raw = explode(packed)
            assert len(raw) == size and raw[:2] == b'BM', name
            assert explode(implode(raw)) == raw
        print('all', len(entries), 'entries decode and round-trip')
    elif cmd == 'extract':  # extract <idx> <dat> <name> <out.bmp>
        _, _, entries = read_archive(sys.argv[2], sys.argv[3])
        for name, packed, size in entries:
            if name.lower() == sys.argv[4].lower():
                open(sys.argv[5], 'wb').write(explode(packed))
    elif cmd == 'put':  # put <idx> <dat> <bmp> <name>...: add or replace entries
        ih, dh, entries = read_archive(sys.argv[2], sys.argv[3])
        raw = open(sys.argv[4], 'rb').read()
        packed = implode(raw)
        for target in sys.argv[5:]:
            entries = [e for e in entries if e[0].lower() != target.lower()]
            entries.append((target, packed, len(raw)))
        entries.sort(key=lambda e: e[0].lower())
        write_archive(sys.argv[2], sys.argv[3], ih, dh, entries)
        print('wrote', len(entries), 'entries')
    else:
        sys.exit(__doc__)
