#!/usr/bin/env python3
"""Generate the small sample dictionaries used as fuzz seeds (mayhem/<target>/testsuite/) and as
mayhem/test.sh fixtures (mayhem/tests/). Run from the repo root: python3 mayhem/tools/mkseeds.py.
The outputs are committed; this script documents how they were made."""
import gzip, io, os, struct, zipfile, zlib

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
WORDS = [("apple", "A round <b>fruit</b> of the apple tree."),
         ("banana", "A long yellow fruit."),
         ("cherry", "A small red stone fruit.")]


def out(rel, data):
    p = os.path.join(ROOT, rel)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p, "wb") as f:
        f.write(data)


def sections(*parts):
    """fuzz_dict.cc multi-file layout: [u32 LE len][bytes] per part, the last part raw."""
    b = b""
    for p in parts[:-1]:
        b += struct.pack("<I", len(p)) + p
    return b + parts[-1]


# ---- DSL (ABBYY Lingvo source, UTF-16LE with BOM) ------------------------------------------
dsl = '#NAME\t"Mayhem Fruits"\n#INDEX_LANGUAGE\t"English"\n#CONTENTS_LANGUAGE\t"English"\n\n'
for w, d in WORDS:
    dsl += "%s\n\t[m1][trn]%s[/trn][/m]\n\t[m1][ex]see [ref]banana[/ref][/ex][/m]\n" % (w, d.replace("<b>", "[b]").replace("</b>", "[/b]"))
dsl_b = b"\xff\xfe" + dsl.encode("utf-16-le")
out("mayhem/tests/fruits.dsl", dsl_b)
out("mayhem/fuzz_dsl/testsuite/fruits.dsl", dsl_b)

# ---- XDXF ------------------------------------------------------------------------------------
x = '<?xml version="1.0" encoding="UTF-8" ?>\n<xdxf lang_from="ENG" lang_to="ENG" format="visual">\n'
x += "<full_name>Mayhem Fruits XDXF</full_name>\n<description>sample</description>\n"
for w, d in WORDS:
    x += "<ar><k>%s</k>%s <kref>banana</kref></ar>\n" % (w, d.replace("<b>", "").replace("</b>", ""))
x += "</xdxf>\n"
out("mayhem/tests/fruits.xdxf", x.encode())
out("mayhem/fuzz_xdxf/testsuite/fruits.xdxf", x.encode())

# ---- GLS (Babylon glossary source) -----------------------------------------------------------
g = ("\n### Glossary title:Mayhem Fruits GLS\n### Author:mayhem\n### Description:sample\n"
     "### Source language:English\n### Target language:English\n### Glossary section:\n")
for w, d in WORDS:
    g += "%s|%s-alt\n%s\n\n" % (w, w, d)
out("mayhem/fuzz_gls/testsuite/fruits.gls", b"\xef\xbb\xbf" + g.encode())
out("mayhem/tests/fruits.gls", b"\xef\xbb\xbf" + g.encode())

# ---- StarDict (.ifo + .idx + .dict) ----------------------------------------------------------
dict_b, idx_b = b"", b""
for w, d in sorted(WORDS):
    art = d.encode()
    idx_b += w.encode() + b"\0" + struct.pack(">II", len(dict_b), len(art))
    dict_b += art
ifo = ("StarDict's dict ifo file\nversion=2.4.2\nwordcount=%d\nidxfilesize=%d\n"
       "bookname=Mayhem Fruits StarDict\nsametypesequence=h\n" % (len(WORDS), len(idx_b))).encode()
out("mayhem/tests/stardict/fruits.ifo", ifo)
out("mayhem/tests/stardict/fruits.idx", idx_b)
out("mayhem/tests/stardict/fruits.dict.bin", dict_b)  # test.sh stages it as fruits.dict
out("mayhem/fuzz_stardict/testsuite/fruits.bin", sections(ifo, idx_b, dict_b))

# ---- dictd (.index + .dict) ------------------------------------------------------------------
B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"


def b64(n):
    s = ""
    while True:
        s = B64[n & 63] + s
        n >>= 6
        if not n:
            return s


body, index = b"", ""
entries = [("00databaseshort", "Mayhem Fruits dictd")] + list(WORDS)
for w, d in entries:
    art = ("%s\n   %s\n" % (w, d)).encode()
    index += "%s\t%s\t%s\n" % (w, b64(len(body)), b64(len(art)))
    body += art
out("mayhem/fuzz_dictd/testsuite/fruits.bin", sections(index.encode(), body))

# ---- ZipSounds (.zips: a zip of audio files, names are the headwords) ----------------------------
zb = io.BytesIO()
with zipfile.ZipFile(zb, "w", zipfile.ZIP_DEFLATED) as z:
    for w, _ in WORDS:
        z.writestr(w + ".wav", b"RIFF\x24\0\0\0WAVEfmt " + b"\0" * 24)
out("mayhem/fuzz_zipsounds/testsuite/fruits.zips", zb.getvalue())

# ---- MDict (.mdx, engine 2.0, zlib blocks) ---------------------------------------------------


def zblock(data):
    return struct.pack("<I", 2) + struct.pack(">I", zlib.adler32(data) & 0xffffffff) + zlib.compress(data)


hdr = ('<Dictionary GeneratedByEngineVersion="2.0" RequiredEngineVersion="2.0" Encrypted="0" '
       'Encoding="UTF-8" Format="Html" Title="Mayhem Fruits MDX" Description="sample"/>\r\n\0').encode("utf-16-le")
mdx = struct.pack(">I", len(hdr)) + hdr + struct.pack("<I", zlib.adler32(hdr) & 0xffffffff)
records, keys = b"", b""
for w, d in sorted(WORDS):
    keys += struct.pack(">Q", len(records)) + w.encode() + b"\0"
    records += d.encode() + b"\0"
kblock = zblock(keys)
first, last = sorted(WORDS)[0][0].encode(), sorted(WORDS)[-1][0].encode()
kinfo_raw = (struct.pack(">Q", len(WORDS)) + struct.pack(">H", len(first)) + first + b"\0"
             + struct.pack(">H", len(last)) + last + b"\0" + struct.pack(">QQ", len(kblock), len(keys)))
kinfo = zblock(kinfo_raw)
khead = struct.pack(">QQQQQ", 1, len(WORDS), len(kinfo_raw), len(kinfo), len(kblock))
mdx += khead + struct.pack(">I", zlib.adler32(khead) & 0xffffffff) + kinfo + kblock
rblock = zblock(records)
mdx += struct.pack(">QQQQ", 1, len(WORDS), 16, len(rblock)) + struct.pack(">QQ", len(rblock), len(records)) + rblock
out("mayhem/tests/fruits.mdx", mdx)
out("mayhem/fuzz_mdx/testsuite/fruits.mdx", mdx)

# ---- Babylon (.bgl): 6-byte header, then a gzip stream of [type|lencode][len][data] blocks -------


def bblock(btype, data):
    if len(data) < 256:
        return bytes([btype]) + bytes([len(data)]) + data          # lencode 0: 1-byte length
    return bytes([0x10 | btype]) + struct.pack(">H", len(data)) + data  # lencode 1: 2-byte length


body = bblock(0, b"\x08\x00\x41")                    # default charset: Default (latin1)
body += bblock(3, b"\x00\x01Mayhem Fruits BGL")      # title
body += bblock(3, b"\x00\x02mayhem")                 # author
body += bblock(3, b"\x00\x09A sample\nBabylon glossary.")  # description
body += bblock(3, b"\x00\x1a\x41")                   # source charset
body += bblock(3, b"\x00\x1b\x41")                   # target charset
body += bblock(3, b"\x00\x11\x00\x00\x80")           # utf-8 flag
body += bblock(2, b"\x08fruit.bmp" + b"BM" + b"\0" * 12)   # embedded resource
for w, d in WORDS:
    dd = d.encode() + bytes([0x18, len(w)]) + w.capitalize().encode() + b"\x14\x02\x30"  # displayed hw + part of speech
    body += bblock(1, bytes([len(w)]) + w.encode() + struct.pack(">H", len(dd)) + dd)
alt = b"apples"
dd = b"Plural of apple."
body += bblock(11, b"\x00" + struct.pack(">I", len(alt)) + alt + struct.pack(">I", 1)
               + struct.pack(">I", 5) + b"pomes" + struct.pack(">I", len(dd)) + dd + b"\x00")
body += bytes([4])                                   # end-of-file marker
bgl = b"\x12\x34\x00\x01\x00\x06" + gzip.compress(body, mtime=0)
out("mayhem/tests/fruits.bgl", bgl)
out("mayhem/fuzz_bgl/testsuite/fruits.bgl", bgl)
