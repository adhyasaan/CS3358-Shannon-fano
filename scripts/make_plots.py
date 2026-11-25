#!/usr/bin/env python3
"""Make the report figures from results/results.csv.  Usage: python3 scripts/make_plots.py"""
import csv, math, os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

C1, C2, C3 = "#2a78d6", "#eb6834", "#1baf7a"
INK, INK2, GRID = "#0b0b0b", "#52514e", "#e4e3de"
plt.rcParams.update({
    "font.family": "DejaVu Sans", "font.size": 10, "axes.edgecolor": INK2, "axes.labelcolor": INK,
    "xtick.color": INK2, "ytick.color": INK2, "axes.spines.top": False, "axes.spines.right": False,
    "axes.grid": True, "grid.color": GRID, "grid.linewidth": 0.8, "lines.linewidth": 2,
    "legend.frameon": False, "figure.dpi": 150, "savefig.bbox": "tight",
})
os.makedirs("figures", exist_ok=True)
rows = {r["file"]: r for r in csv.DictReader(open("results/results.csv"))}
f = lambda r, k: float(r[k])

# Fig 1: running time vs input size (log-log), E1
e1 = sorted([r for n, r in rows.items() if n.startswith("e1_")], key=lambda r: f(r, "bytes"))
N = [f(r, "bytes") for r in e1]
fig, ax = plt.subplots(figsize=(6.4, 4))
for key, lab, c, m in [("encode_ms", "Compress (file in -> .sf file out)", C1, "o"),
                       ("decode_ms", "Decompress (.sf in -> file out)", C2, "s"),
                       ("build_encode_ms", "Build tree + encode (in memory)", C3, "^")]:
    ax.plot(N, [f(r, key) for r in e1], marker=m, markersize=6, color=c, label=lab)
ref = [f(e1[-1], "decode_ms") * n / N[-1] for n in N]
ax.plot(N, ref, ls="--", lw=1.2, color=INK2, label="Reference slope: O(N)")
ax.set_xscale("log"); ax.set_yscale("log")
ax.set_xlabel("Input size N (bytes, log scale)"); ax.set_ylabel("Median time (ms, log scale)")
ax.set_title("Fig. 1  Running time vs. input size (26 letters, uniform)", loc="left", color=INK)
ax.legend(fontsize=8.5); fig.savefig("figures/fig1_time_vs_size.png"); plt.close(fig)

# Fig 2: average code length vs log2(k), E2
e2 = sorted([r for n, r in rows.items() if n.startswith("e2_")], key=lambda r: f(r, "distinct"))
k = [f(r, "distinct") for r in e2]
fig, ax = plt.subplots(figsize=(6.4, 4))
ax.plot(k, [f(r, "avg_code_len") for r in e2], marker="o", color=C1, label="Shannon-Fano bits per symbol")
ax.plot(k, [f(r, "entropy_bpc") for r in e2], marker="s", markersize=5, ls=":", color=C2, label="Entropy H (lower bound)")
ax.plot(k, [f(r, "max_code_len") for r in e2], marker="^", ls="--", lw=1.4, color=C3, label="Tree depth (longest code)", zorder=1)
ax.set_xscale("log", base=2); ax.set_xticks(k); ax.set_xticklabels([str(int(x)) for x in k])
ax.set_xlabel("Distinct symbols k (log2 scale), N = 1,000,000"); ax.set_ylabel("Bits")
ax.set_title("Fig. 2  Code length vs. alphabet size (uniform)", loc="left", color=INK)
ax.legend(fontsize=8.5); fig.savefig("figures/fig2_alphabet.png"); plt.close(fig)

# Fig 3: bits/symbol vs entropy per dataset (E3 + E4)
names = ["e3_dist_uniform.txt", "e3_dist_zipf.txt", "e3_dist_geometric.txt", "e4_real_gpl3.txt",
         "e4_real_licenses_all.txt", "e4_real_python_src.py", "e4_real_executable.bin", "e4_real_gzipped.gz"]
labels = ["uniform", "zipf", "geometric", "GPL-3 text", "all licenses", "Python src", "executable", "gzip file"]
H = [f(rows[n], "entropy_bpc") for n in names]; L = [f(rows[n], "avg_code_len") for n in names]
x = range(len(names)); w = 0.38
fig, ax = plt.subplots(figsize=(7.2, 4))
ax.bar([i - w / 2 - 0.01 for i in x], H, w, color=C2, label="Entropy H")
ax.bar([i + w / 2 + 0.01 for i in x], L, w, color=C1, label="Shannon-Fano avg. code length")
ax.axhline(8, color=INK2, lw=1, ls="--"); ax.text(2, 8.12, "8 bits = no compression", ha="center", fontsize=8, color=INK2)
for i, (h, l) in enumerate(zip(H, L)):
    ax.text(i, max(h, l) + 0.15, f"+{l - h:.3f}", ha="center", fontsize=7.5, color=INK2)
ax.set_xticks(list(x)); ax.set_xticklabels(labels, rotation=20, ha="right")
ax.set_ylabel("Bits per symbol"); ax.set_ylim(0, 9)
ax.set_title("Fig. 3  Average code length vs. entropy (gap shown above bars)", loc="left", color=INK, pad=28)
ax.legend(fontsize=8.5, loc="lower left", bbox_to_anchor=(0, 1.0), ncol=2); fig.savefig("figures/fig3_entropy_gap.png"); plt.close(fig)

# Fig 4: time vs total code bits (all datasets with N >= 100,000 and k >= 2)
big = [r for r in rows.values() if f(r, "bytes") >= 1e5 and f(r, "payload_bits") > 0]
fig, ax = plt.subplots(figsize=(6.4, 4))
ax.scatter([f(r, "payload_bits") / 1e6 for r in big], [f(r, "decode_ms") for r in big], s=36, color=C2,
           edgecolor="white", linewidth=1, label="Decompress", zorder=3)
ax.scatter([f(r, "payload_bits") / 1e6 for r in big], [f(r, "build_encode_ms") for r in big], s=36, color=C1,
           marker="^", edgecolor="white", linewidth=1, label="Build tree + encode", zorder=3)
ax.set_xscale("log"); ax.set_yscale("log")
ax.set_xlabel("Total code bits written (millions, log scale)"); ax.set_ylabel("Median time (ms, log scale)")
ax.set_title("Fig. 4  Time grows with total code bits (N x avg. length)", loc="left", color=INK)
ax.legend(fontsize=8.5); fig.savefig("figures/fig4_time_vs_bits.png"); plt.close(fig)

# Fig 5: code tree for the textbook example A15 B7 C6 D6 E5
fig, ax = plt.subplots(figsize=(6.4, 3.4)); ax.axis("off"); ax.grid(False)
nodes = {"root": (3, 3, "39"), "0": (1.5, 2, "22"), "1": (4.5, 2, "17"),
         "00": (0.8, 1, "A:15\n00"), "01": (2.2, 1, "B:7\n01"), "10": (3.8, 1, "C:6\n10"), "11": (5.2, 1, "11"),
         "110": (4.5, 0, "D:6\n110"), "111": (5.9, 0, "E:5\n111")}
edges = [("root", "0", "0"), ("root", "1", "1"), ("0", "00", "0"), ("0", "01", "1"), ("1", "10", "0"),
         ("1", "11", "1"), ("11", "110", "0"), ("11", "111", "1")]
for a, b, bit in edges:
    (x1, y1, _), (x2, y2, _) = nodes[a], nodes[b]
    ax.plot([x1, x2], [y1, y2], color=INK2, lw=1.2, zorder=1)
    ax.text((x1 + x2) / 2 + (-0.12 if bit == "0" else 0.12), (y1 + y2) / 2 + 0.05, bit, color=C2, fontsize=10, ha="center", weight="bold")
for key, (x0, y0, t) in nodes.items():
    leaf = "\n" in t
    ax.text(x0, y0, t, ha="center", va="center", fontsize=9, color=INK, zorder=2,
            bbox=dict(boxstyle="round,pad=0.35", fc="#dcebfa" if leaf else "white", ec=C1 if leaf else INK2, lw=1.2))
ax.set_xlim(0.2, 6.5); ax.set_ylim(-0.5, 3.4)
ax.set_title("Fig. 5  Shannon-Fano tree for A15 B7 C6 D6 E5 (89 bits)", loc="left", color=INK)
fig.savefig("figures/fig5_tree_example.png"); plt.close(fig)
print("figures written")
