"""Minimal PE reader: RVA <-> file offset, section map, and pattern search.

Everything here works on the FILE, not a running process, so a signature can
be tested against Complete Edition without launching it.
"""
import struct


class PE(object):
    def __init__(self, path):
        self.path = path
        with open(path, "rb") as f:
            self.data = f.read()

        d = self.data
        pe = struct.unpack_from("<I", d, 0x3C)[0]
        assert d[pe:pe + 4] == b"PE\x00\x00", "not a PE"

        self.machine = struct.unpack_from("<H", d, pe + 4)[0]
        nsec = struct.unpack_from("<H", d, pe + 6)[0]
        optsize = struct.unpack_from("<H", d, pe + 20)[0]
        opt = pe + 24

        magic = struct.unpack_from("<H", d, opt)[0]
        self.pe32plus = magic == 0x20B
        self.image_base = (struct.unpack_from("<Q", d, opt + 24)[0] if self.pe32plus
                           else struct.unpack_from("<I", d, opt + 28)[0])
        self.size_of_image = struct.unpack_from("<I", d, opt + 56)[0]

        self.sections = []
        s = opt + optsize
        for i in range(nsec):
            o = s + i * 40
            name = d[o:o + 8].rstrip(b"\x00").decode("latin-1")
            vsize, vaddr, rawsize, rawptr = struct.unpack_from("<IIII", d, o + 8)
            chars = struct.unpack_from("<I", d, o + 36)[0]
            self.sections.append({
                "name": name, "vaddr": vaddr, "vsize": vsize,
                "rawptr": rawptr, "rawsize": rawsize, "chars": chars,
                "exec": bool(chars & 0x20000000),
            })

    def rva_to_off(self, rva):
        for s in self.sections:
            if s["vaddr"] <= rva < s["vaddr"] + max(s["vsize"], s["rawsize"]):
                o = rva - s["vaddr"] + s["rawptr"]
                if o < len(self.data):
                    return o
        return None

    def off_to_rva(self, off):
        for s in self.sections:
            if s["rawptr"] <= off < s["rawptr"] + s["rawsize"]:
                return off - s["rawptr"] + s["vaddr"]
        return None

    def read(self, rva, n):
        o = self.rva_to_off(rva)
        if o is None:
            return None
        return self.data[o:o + n]

    def read_u32(self, rva):
        b = self.read(rva, 4)
        return None if b is None or len(b) < 4 else struct.unpack("<I", b)[0]

    def read_f32(self, rva):
        b = self.read(rva, 4)
        return None if b is None or len(b) < 4 else struct.unpack("<f", b)[0]

    def section_of(self, rva):
        for s in self.sections:
            if s["vaddr"] <= rva < s["vaddr"] + max(s["vsize"], s["rawsize"]):
                return s["name"]
        return None

    def find(self, sig, exec_only=True):
        """sig: 'F3 0F 10 05 ?? ?? ?? ??'. Returns list of RVAs."""
        parts = sig.split()
        pat = [None if p[0] == "?" else int(p, 16) for p in parts]
        n = len(pat)
        hits = []
        for s in self.sections:
            if exec_only and not s["exec"]:
                continue
            blob = self.data[s["rawptr"]:s["rawptr"] + s["rawsize"]]
            first = pat[0]
            i = 0
            while True:
                if first is None:
                    break
                i = blob.find(bytes([first]), i)
                if i < 0 or i + n > len(blob):
                    break
                ok = True
                for k in range(1, n):
                    if pat[k] is not None and blob[i + k] != pat[k]:
                        ok = False
                        break
                if ok:
                    hits.append(s["vaddr"] + i)
                i += 1
        return hits

    def find_bytes(self, raw, exec_only=False):
        hits = []
        for s in self.sections:
            if exec_only and not s["exec"]:
                continue
            blob = self.data[s["rawptr"]:s["rawptr"] + s["rawsize"]]
            i = 0
            while True:
                i = blob.find(raw, i)
                if i < 0:
                    break
                hits.append(s["vaddr"] + i)
                i += 1
        return hits

    def hexdump(self, rva, n, per=16):
        b = self.read(rva, n)
        if b is None:
            return "(unmapped)"
        out = []
        for i in range(0, len(b), per):
            row = b[i:i + per]
            out.append("%08X  %-*s" % (rva + i, per * 3,
                       " ".join("%02X" % c for c in row)))
        return "\n".join(out)
