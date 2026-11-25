# Project 5B – Lossless Data Compression via Shannon-Fano Coding (Part B)

CS3358 Data Structures & Algorithms – Honors Credit Project, Texas State University
Student: Sandhya Thapa (Part B, Shannon-Fano) · Team: Shreya (Part A, Huffman), Prasun (Part D, Evaluation)
Faculty: Suddhasvatta Das

A complete file compressor/decompressor using Shannon-Fano coding: explicit top-down code tree,
bit packing, a self-describing `.sf` file format, and byte-exact decompression.

## Build

```bash
g++ -std=c++17 -O2 -Wall -Wextra src/shannon_fano.cpp -o bin/sf
```

## Use

```bash
./bin/sf compress   input.txt  input.sf      # compress any file (text or binary)
./bin/sf decompress input.sf   restored.txt  # restore it
cmp input.txt restored.txt                    # no output = identical

./bin/sf test                                  # 17 correctness tests
./bin/sf codes input.txt                       # symbol,frequency,code,length
./bin/sf stats --runs 7 datasets/*             # one CSV row per file (timed + verified)
```

## Reproduce all results

```bash
mkdir -p bin && g++ -std=c++17 -O2 src/shannon_fano.cpp -o bin/sf
python3 scripts/make_datasets.py datasets      # 28 files, fixed seed 42
./bin/sf test
./bin/sf stats --runs 7 datasets/* > results/results.csv
python3 scripts/make_plots.py                  # figures/fig1..fig5
```

Real-file datasets (E4) are copied from a standard Ubuntu 24.04 install
(`/usr/share/common-licenses`, `/usr/lib/python3*/*.py`, `/usr/bin/ls`); on another OS, swap in
any comparable files.

## `.sf` file format (little-endian)

| Bytes | Field |
|---|---|
| 4 | magic `SFC2` |
| 8 | original length N |
| 2 | k = number of distinct byte values |
| 9 × k | (1-byte symbol, 8-byte frequency) |
| rest | code bits, MSB first, last byte zero-padded |

The decoder rebuilds the identical tree from the frequency table (deterministic tie rules:
equal counts → smaller byte first; equal split differences → first cut), then walks the tree
and stops after exactly N symbols. A file with one distinct symbol stores no code bits.

## For Part A / Part D (comparison with Huffman)

- Use the same 28 files from `scripts/make_datasets.py` (fixed seed → identical bytes).
- `results/results.csv` columns: `file, bytes, distinct, entropy_bpc, max_freq, min_freq, mean_freq,
  sd_freq, payload_bits, header_bytes, compressed_bytes, avg_code_len, max_code_len, ratio,
  space_saving_pct, build_encode_ms, encode_ms, decode_ms, prefix_free, kraft_sum, roundtrip_ok`.
- `./bin/sf codes <file>` gives per-symbol code lengths for tree-shape comparison.

## Layout

```
src/shannon_fano.cpp      compressor, decompressor, verification, tests, stats
scripts/make_datasets.py  dataset generator (E1–E5)
scripts/make_plots.py     figures from results/results.csv
results/results.csv       measured results
figures/                  report figures
```
