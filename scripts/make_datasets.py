#!/usr/bin/env python3
"""Generate the Part B datasets as real files (fixed seed -> identical every run).

Usage: python3 scripts/make_datasets.py [output_dir]   (default: datasets/)

Experiments vary ONE parameter at a time:
  E1 size         : uniform over 26 lowercase letters, N = 1e3 .. 1e7 bytes
  E2 alphabet     : uniform, N = 1e6 bytes, k = 2, 4, ..., 256 distinct byte values
  E3 distribution : N = 1e6 bytes, 26 letters, single / uniform / zipf / geometric
  E4 real files   : English license text, source code, an executable, a gzip file
  E5 edge/hand    : empty, single byte, worked examples from the report
The same files can be given to Part A (Huffman) so Part D compares like with like.
"""
import gzip, glob, os, random, sys

out = sys.argv[1] if len(sys.argv) > 1 else "datasets"
os.makedirs(out, exist_ok=True)
rng = random.Random(42)
LET = b"abcdefghijklmnopqrstuvwxyz"

def write(name, data):
    with open(os.path.join(out, name), "wb") as f:
        f.write(data)

def sample(n, alphabet, weights=None):
    return bytes(rng.choices(alphabet, weights=weights, k=n))

# E1: size
for n in (1_000, 10_000, 100_000, 1_000_000, 10_000_000):
    write(f"e1_size_{n}.txt", sample(n, LET))

# E2: alphabet size (byte values 0..k-1)
for k in (2, 4, 8, 16, 32, 64, 128, 256):
    write(f"e2_alpha_{k:03d}.bin", sample(1_000_000, list(range(k))))

# E3: distribution shape
N = 1_000_000
write("e3_dist_single.txt", b"a" * N)
write("e3_dist_uniform.txt", sample(N, LET))
write("e3_dist_zipf.txt", sample(N, LET, [1 / (i + 1) for i in range(26)]))
write("e3_dist_geometric.txt", sample(N, LET, [0.5 ** (i + 1) for i in range(26)]))

# E4: real files (taken from the operating system, so anyone can reproduce them)
def readall(p):
    with open(p, "rb") as f:
        return f.read()
write("e4_real_gpl3.txt", readall("/usr/share/common-licenses/GPL-3"))
lic = b"".join(readall(p) for p in sorted(glob.glob("/usr/share/common-licenses/*")) if os.path.isfile(p))
write("e4_real_licenses_all.txt", lic)
py = sorted(glob.glob("/usr/lib/python3*/*.py"))
src = b"".join(readall(p) for p in py)[:2_000_000]
write("e4_real_python_src.py", src)
write("e4_real_executable.bin", readall("/usr/bin/ls"))
write("e4_real_gzipped.gz", gzip.compress(src, mtime=0))

# E5: edge cases and hand-worked examples
write("e5_empty.txt", b"")
write("e5_one_byte.txt", b"Z")
write("e5_textbook_A15B7C6D6E5.txt", b"A" * 15 + b"B" * 7 + b"C" * 6 + b"D" * 6 + b"E" * 5)
write("e5_ten_equal.txt", b"ABCDEFGHIJ")
write("e5_mississippi.txt", b"MISSISSIPPI")
write("e5_spec_A5B2C2D2E2.txt", b"AAAAABBCCDDEE")   # spec sub-optimality case

print("datasets written to", out)
