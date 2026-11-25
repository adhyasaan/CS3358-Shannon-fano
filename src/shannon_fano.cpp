// Shannon-Fano Coding - Project 5B (Version 2: full file pipeline)
// Lossless Data Compression via Shannon-Fano Coding
// CS3358 Honors Credit Project, Texas State University
// Author: Sandhya Thapa (Part B)
//
// Version 2 adds, on top of the version 1 core algorithm:
//   * real file input/output (binary mode, any byte value 0..255)
//   * an explicit binary code tree (leaf = symbol, internal = split)
//   * bit packing into bytes + a header storing the frequency table
//   * decoding by walking the tree, stopping after the original length
//     (so the zero padding in the last byte is never decoded)
//   * verification: prefix-free check, Kraft sum, byte-exact round trip
//   * a CSV "stats" mode and a "codes" mode so Part D (evaluation) can
//     compare against Part A (Huffman) on exactly the same files
//
// Compile:  g++ -std=c++17 -O2 -Wall -Wextra shannon_fano.cpp -o sf
// Usage:
//   ./sf compress   <input> <output.sf>
//   ./sf decompress <input.sf> <output>
//   ./sf test                      (correctness tests)
//   ./sf stats <file> [<file>...]  (one CSV row per file, see header)
//   ./sf codes <file>              (symbol,frequency,code,length table)
//
// Compressed file format (.sf), all integers little-endian:
//   4 bytes   magic "SFC2"
//   8 bytes   original length N (number of bytes)
//   2 bytes   k = number of distinct symbols (0..256)
//   k * 9     (1 byte symbol, 8 bytes frequency), in byte-value order
//   rest      packed code bits, most significant bit first,
//             last byte padded with 0 bits
// The decoder rebuilds the identical tree from the frequency table,
// because the sort and the split rule are deterministic.
//
// Tie rules (so the output is the same on every run):
//   sort:  equal frequencies -> smaller byte value first
//   split: equal differences -> the first (leftmost) cut wins

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
using namespace std;

typedef vector<uint8_t> Bytes;
typedef array<uint64_t, 256> FreqTable;

struct Symbol { uint8_t sym; uint64_t freq; };

// ---------------------------------------------------------------
// Explicit code tree. Nodes live in a vector; children are indices.
// ---------------------------------------------------------------
struct Node {
    int left = -1, right = -1;   // -1 means "no child" (leaf)
    uint8_t sym = 0;             // valid only for leaves
    uint64_t weight = 0;         // total frequency below this node
    bool isLeaf() const { return left < 0 && right < 0; }
};

struct CodeTree {
    vector<Node> nodes;
    int root = -1;
    array<string, 256> code;     // code[b] = "0101..." ("" if b unused)
    int depth = 0;               // longest code length
};

// STEP 1: count frequencies. O(N)
FreqTable countFrequencies(const Bytes& data) {
    FreqTable f{};
    for (uint8_t b : data) f[b]++;
    return f;
}

// STEP 2: sort descending by frequency (tie: smaller byte first). O(k log k)
vector<Symbol> sortSymbols(const FreqTable& f) {
    vector<Symbol> s;
    for (int b = 0; b < 256; b++) if (f[b]) s.push_back({(uint8_t)b, f[b]});
    sort(s.begin(), s.end(), [](const Symbol& a, const Symbol& b) {
        if (a.freq != b.freq) return a.freq > b.freq;
        return a.sym < b.sym;
    });
    return s;
}

// STEP 3: most balanced cut of s[lo..hi-1]; returns first index of
// the bottom group. Linear scan with a running sum. O(hi - lo)
int findSplit(const vector<Symbol>& s, int lo, int hi) {
    uint64_t total = 0;
    for (int i = lo; i < hi; i++) total += s[i].freq;
    uint64_t top = 0, bestDiff = UINT64_MAX;
    int best = lo + 1;
    for (int i = lo; i < hi - 1; i++) {
        top += s[i].freq;
        uint64_t bottom = total - top;
        uint64_t diff = top > bottom ? top - bottom : bottom - top;
        if (diff < bestDiff) { bestDiff = diff; best = i + 1; }  // strict <: first cut wins ties
    }
    return best;
}

// STEP 4: build the tree top-down. Returns the index of the new node.
int buildNode(const vector<Symbol>& s, int lo, int hi, CodeTree& t) {
    Node n;
    for (int i = lo; i < hi; i++) n.weight += s[i].freq;
    if (hi - lo == 1) {                       // one symbol left -> leaf
        n.sym = s[lo].sym;
        t.nodes.push_back(n);
        return (int)t.nodes.size() - 1;
    }
    int cut = findSplit(s, lo, hi);
    int l = buildNode(s, lo, cut, t);         // top group -> bit 0
    int r = buildNode(s, cut, hi, t);         // bottom group -> bit 1
    n.left = l; n.right = r;
    t.nodes.push_back(n);
    return (int)t.nodes.size() - 1;
}

void assignCodes(CodeTree& t, int id, string& path) {
    const Node& n = t.nodes[id];
    if (n.isLeaf()) {
        t.code[n.sym] = path.empty() ? "0" : path;   // single-symbol input gets "0"
        t.depth = max(t.depth, (int)t.code[n.sym].size());
        return;
    }
    path.push_back('0'); assignCodes(t, n.left, path);  path.pop_back();
    path.push_back('1'); assignCodes(t, n.right, path); path.pop_back();
}

CodeTree buildTree(const FreqTable& f) {
    CodeTree t;
    vector<Symbol> s = sortSymbols(f);
    if (s.empty()) return t;                  // empty input -> empty tree
    t.nodes.reserve(2 * s.size());
    t.root = buildNode(s, 0, (int)s.size(), t);
    string path;
    assignCodes(t, t.root, path);
    return t;
}

// ---------------------------------------------------------------
// STEP 5: encode = look up each byte's code and pack bits into bytes.
// ---------------------------------------------------------------
struct BitWriter {
    Bytes out; uint8_t cur = 0; int n = 0; uint64_t bits = 0;
    void put(int bit) {
        cur = (uint8_t)((cur << 1) | bit);
        bits++;
        if (++n == 8) { out.push_back(cur); cur = 0; n = 0; }
    }
    void flush() { if (n) { out.push_back((uint8_t)(cur << (8 - n))); cur = 0; n = 0; } }
};

Bytes encodeBits(const Bytes& data, const CodeTree& t, uint64_t& payloadBits) {
    BitWriter w;
    // Only one distinct symbol: the header (symbol + count N) already
    // determines the file, so no code bits are written at all.
    if (t.root >= 0 && t.nodes[t.root].isLeaf()) { payloadBits = 0; return w.out; }
    w.out.reserve(data.size() / 2 + 16);
    for (uint8_t b : data)
        for (char c : t.code[b]) w.put(c == '1');
    payloadBits = w.bits;
    w.flush();
    return w.out;
}

// STEP 6: decode by walking the tree from the root; emit at each leaf.
// Stops after exactly N symbols, so padding bits are ignored. O(total bits)
Bytes decodeBits(const Bytes& packed, size_t offset, const CodeTree& t, uint64_t N) {
    Bytes out;
    out.reserve(N);
    if (N == 0) return out;
    if (t.nodes[t.root].isLeaf()) {           // single symbol: no bits stored, just repeat it N times
        out.assign(N, t.nodes[t.root].sym);
        return out;
    }
    int id = t.root;
    for (size_t i = offset; i < packed.size() && out.size() < N; i++) {
        for (int k = 7; k >= 0 && out.size() < N; k--) {
            int bit = (packed[i] >> k) & 1;
            id = bit ? t.nodes[id].right : t.nodes[id].left;
            if (t.nodes[id].isLeaf()) { out.push_back(t.nodes[id].sym); id = t.root; }
        }
    }
    if (out.size() != N) throw runtime_error("corrupt input: ran out of bits");
    return out;
}

// ---------------------------------------------------------------
// Container format: header (frequency table) + packed bits
// ---------------------------------------------------------------
void putU64(Bytes& o, uint64_t v) { for (int i = 0; i < 8; i++) o.push_back((uint8_t)(v >> (8 * i))); }
uint64_t getU64(const Bytes& in, size_t& p) {
    if (p + 8 > in.size()) throw runtime_error("corrupt header");
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= (uint64_t)in[p + i] << (8 * i);
    p += 8; return v;
}

struct CompressResult { Bytes file; uint64_t payloadBits = 0; size_t headerBytes = 0; CodeTree tree; };

CompressResult compressBytes(const Bytes& data) {
    CompressResult r;
    FreqTable f = countFrequencies(data);
    r.tree = buildTree(f);
    Bytes& o = r.file;
    o.insert(o.end(), {'S', 'F', 'C', '2'});
    putU64(o, data.size());
    uint16_t k = 0;
    for (int b = 0; b < 256; b++) if (f[b]) k++;
    o.push_back((uint8_t)(k & 0xFF)); o.push_back((uint8_t)(k >> 8));
    for (int b = 0; b < 256; b++) if (f[b]) { o.push_back((uint8_t)b); putU64(o, f[b]); }
    r.headerBytes = o.size();
    Bytes body = encodeBits(data, r.tree, r.payloadBits);
    o.insert(o.end(), body.begin(), body.end());
    return r;
}

Bytes decompressBytes(const Bytes& file) {
    if (file.size() < 14 || memcmp(file.data(), "SFC2", 4) != 0) throw runtime_error("not an .sf file");
    size_t p = 4;
    uint64_t N = getU64(file, p);
    uint16_t k = (uint16_t)(file[p] | (file[p + 1] << 8)); p += 2;
    FreqTable f{};
    for (int i = 0; i < k; i++) {
        if (p >= file.size()) throw runtime_error("corrupt header");
        uint8_t b = file[p++];
        f[b] = getU64(file, p);
    }
    CodeTree t = buildTree(f);
    return decodeBits(file, p, t, N);
}

// ---------------------------------------------------------------
// File helpers
// ---------------------------------------------------------------
Bytes readFile(const string& path) {
    ifstream in(path, ios::binary);
    if (!in) throw runtime_error("cannot open " + path);
    return Bytes((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
}
void writeFile(const string& path, const Bytes& b) {
    ofstream out(path, ios::binary);
    if (!out) throw runtime_error("cannot write " + path);
    out.write((const char*)b.data(), (streamsize)b.size());
}
string baseName(const string& p) { size_t s = p.find_last_of("/\\"); return s == string::npos ? p : p.substr(s + 1); }

// ---------------------------------------------------------------
// Verification helpers
// ---------------------------------------------------------------
// Prefix-free: after sorting codes as strings, a prefix would sit
// directly before a code it is a prefix of, so checking neighbours suffices.
bool isPrefixFree(const CodeTree& t) {
    vector<string> c;
    for (const string& s : t.code) if (!s.empty()) c.push_back(s);
    sort(c.begin(), c.end());
    for (size_t i = 1; i < c.size(); i++)
        if (c[i].compare(0, c[i - 1].size(), c[i - 1]) == 0) return false;
    return true;
}
// Kraft sum = sum 2^-len. A full binary tree gives exactly 1.
double kraftSum(const CodeTree& t) {
    double s = 0;
    for (const string& c : t.code) if (!c.empty()) s += ldexp(1.0, -(int)c.size());
    return s;
}
double entropyBits(const FreqTable& f, uint64_t N) {
    double h = 0;
    for (uint64_t c : f) if (c) { double p = (double)c / N; h -= p * log2(p); }
    return h;
}
uint64_t totalBits(const FreqTable& f, const CodeTree& t) {
    uint64_t s = 0;
    for (int b = 0; b < 256; b++) if (f[b]) s += f[b] * t.code[b].size();
    return s;
}
// Optimal (Huffman) total cost, used ONLY as a reference number in the tests
// to back up the "Huffman wins" example. Not the Part A implementation.
uint64_t optimalCostReference(const FreqTable& f) {
    vector<uint64_t> w;
    for (uint64_t c : f) if (c) w.push_back(c);
    if (w.size() < 2) return w.empty() ? 0 : w[0];
    uint64_t cost = 0;
    while (w.size() > 1) {
        sort(w.begin(), w.end(), greater<uint64_t>());
        uint64_t a = w.back(); w.pop_back();
        uint64_t b = w.back(); w.pop_back();
        cost += a + b; w.push_back(a + b);
    }
    return cost;
}

// ---------------------------------------------------------------
// Correctness tests
// ---------------------------------------------------------------
int passed = 0, failed = 0;
void check(const string& name, bool ok) {
    cout << (ok ? "  PASS  " : "  FAIL  ") << name << "\n";
    (ok ? passed : failed)++;
}
Bytes B(const string& s) { return Bytes(s.begin(), s.end()); }
bool roundTrip(const Bytes& d) { return decompressBytes(compressBytes(d).file) == d; }

int runTests() {
    cout << "CORRECTNESS TESTS\n";
    // 1. Textbook example A15 B7 C6 D6 E5
    Bytes w = B(string(15, 'A') + string(7, 'B') + string(6, 'C') + string(6, 'D') + string(5, 'E'));
    CodeTree t1 = buildTree(countFrequencies(w));
    check("A15 B7 C6 D6 E5 -> A=00 B=01 C=10 D=110 E=111",
          t1.code['A'] == "00" && t1.code['B'] == "01" && t1.code['C'] == "10" &&
          t1.code['D'] == "110" && t1.code['E'] == "111");
    uint64_t sf1 = totalBits(countFrequencies(w), t1), hf1 = optimalCostReference(countFrequencies(w));
    check("A15 B7 C6 D6 E5 -> Shannon-Fano 89 bits, optimal (Huffman) 87 bits",
          sf1 == 89 && hf1 == 87);
    // 1b. Specification sub-optimality case: A5 B2 C2 D2 E2 ("AAAAABBCCDDEE")
    Bytes spec = B("AAAAABBCCDDEE");
    CodeTree ts = buildTree(countFrequencies(spec));
    uint64_t sfPayload = 0;
    encodeBits(spec, ts, sfPayload);          // payload bits only: no header, no padding
    check("Spec case AAAAABBCCDDEE -> A=00 B=01 C=10 D=110 E=111",
          ts.code['A'] == "00" && ts.code['B'] == "01" && ts.code['C'] == "10" &&
          ts.code['D'] == "110" && ts.code['E'] == "111");
    check("Spec case AAAAABBCCDDEE -> Shannon-Fano 30 payload bits, optimal (Huffman) 29, SF >= Huffman",
          sfPayload == 30 && optimalCostReference(countFrequencies(spec)) == 29 &&
          sfPayload >= optimalCostReference(countFrequencies(spec)) && roundTrip(spec));
    // 2. Corrected 10-equal-characters example: 34 bits for both
    Bytes ten = B("ABCDEFGHIJ");
    CodeTree t2 = buildTree(countFrequencies(ten));
    check("10 equal characters -> 34 bits (6 codes x 3 + 4 codes x 4), equal to optimal",
          totalBits(countFrequencies(ten), t2) == 34 && optimalCostReference(countFrequencies(ten)) == 34);
    // 3. Hand-worked examples
    CodeTree t3 = buildTree(countFrequencies(B("MISSISSIPPI")));
    check("MISSISSIPPI -> I=0 S=10 P=110 M=111 (21 bits)",
          t3.code['I'] == "0" && t3.code['S'] == "10" && t3.code['P'] == "110" && t3.code['M'] == "111" &&
          totalBits(countFrequencies(B("MISSISSIPPI")), t3) == 21);
    CodeTree t4 = buildTree(countFrequencies(B("BANANA")));
    check("BANANA -> A=0 N=10 B=11 (9 bits)",
          t4.code['A'] == "0" && t4.code['N'] == "10" && t4.code['B'] == "11");
    check("ABRACADABRA -> 23 bits", totalBits(countFrequencies(B("ABRACADABRA")),
                                              buildTree(countFrequencies(B("ABRACADABRA")))) == 23);
    // 4. Prefix-free + Kraft on every test tree
    bool pf = true, kr = true;
    for (CodeTree* t : {&t1, &t2, &t3, &t4}) { pf &= isPrefixFree(*t); kr &= fabs(kraftSum(*t) - 1.0) < 1e-12; }
    check("All test trees are prefix-free with Kraft sum = 1", pf && kr);
    // 5. Round trips through the full container format
    check("Round trip: A15 B7 C6 D6 E5", roundTrip(w));
    check("Round trip: English sentence", roundTrip(B("the quick brown fox jumps over the lazy dog\n")));
    Bytes all; for (int r = 0; r < 3; r++) for (int b = 0; b < 256; b++) all.push_back((uint8_t)b);
    check("Round trip: all 256 byte values (binary data)", roundTrip(all));
    // 6. Edge cases
    check("Edge: empty input", roundTrip(Bytes()) && compressBytes(Bytes()).file.size() == 14);
    check("Edge: single byte", roundTrip(B("Z")));
    check("Edge: one repeated symbol (no code bits stored)",
          roundTrip(B(string(1000, 'a'))) && compressBytes(B(string(1000, 'a'))).payloadBits == 0);
    check("Edge: two symbols", roundTrip(B("abababbbba")));
    // 7. Very skewed (Fibonacci-like) weights give the deepest tree
    Bytes fib; uint64_t a = 1, b = 1;
    for (int s = 0; s < 20; s++) { fib.insert(fib.end(), a, (uint8_t)('a' + s)); uint64_t c = a + b; a = b; b = c; }
    CodeTree tf = buildTree(countFrequencies(fib));
    check("Fibonacci weights (20 symbols) -> depth 19, prefix-free, round trip",
          tf.depth == 19 && isPrefixFree(tf) && roundTrip(fib));
    cout << "Result: " << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}

// ---------------------------------------------------------------
// Stats mode: full file pipeline, timed, verified. One CSV row per file.
// encode_ms = read file + count + sort + build tree + encode + write .sf
// decode_ms = read .sf + rebuild tree + decode + write output
// Times are the median of RUNS runs.
// ---------------------------------------------------------------
double medianMs(vector<double> v) { sort(v.begin(), v.end()); return v[v.size() / 2]; }

void statsHeader() {
    cout << "file,bytes,distinct,entropy_bpc,max_freq,min_freq,mean_freq,sd_freq,"
            "payload_bits,header_bytes,compressed_bytes,avg_code_len,max_code_len,"
            "ratio,space_saving_pct,build_encode_ms,encode_ms,decode_ms,"
            "prefix_free,kraft_sum,roundtrip_ok\n";
}

void statsRow(const string& path, int RUNS) {
    string sfPath = path + ".sf.tmp", outPath = path + ".out.tmp";
    vector<double> tb, te, td;
    CompressResult r;
    Bytes data;
    for (int i = 0; i < RUNS; i++) {
        auto t0 = chrono::steady_clock::now();
        data = readFile(path);
        auto t1 = chrono::steady_clock::now();
        r = compressBytes(data);
        auto t2 = chrono::steady_clock::now();
        writeFile(sfPath, r.file);
        auto t3 = chrono::steady_clock::now();
        Bytes back = decompressBytes(readFile(sfPath));
        writeFile(outPath, back);
        auto t4 = chrono::steady_clock::now();
        tb.push_back(chrono::duration<double, milli>(t2 - t1).count());
        te.push_back(chrono::duration<double, milli>(t3 - t0).count());
        td.push_back(chrono::duration<double, milli>(t4 - t3).count());
    }
    bool ok = readFile(outPath) == readFile(path);       // byte-exact comparison of the files on disk
    remove(sfPath.c_str()); remove(outPath.c_str());

    FreqTable f = countFrequencies(data);
    uint64_t N = data.size(), mx = 0, mn = UINT64_MAX; int k = 0;
    double mean = 0, sd = 0;
    for (uint64_t c : f) if (c) { k++; mx = max(mx, c); mn = min(mn, c); }
    if (k) { mean = (double)N / k; for (uint64_t c : f) if (c) sd += (c - mean) * (c - mean); sd = sqrt(sd / k); }
    if (!k) mn = 0;
    double avg = N ? (double)r.payloadBits / N : 0;
    double ratio = r.file.size() ? (double)N / r.file.size() : 0;
    cout << fixed << baseName(path) << "," << N << "," << k << "," << setprecision(4) << (N ? entropyBits(f, N) : 0)
         << "," << mx << "," << mn << "," << setprecision(2) << mean << "," << sd << ","
         << r.payloadBits << "," << r.headerBytes << "," << r.file.size() << ","
         << setprecision(4) << avg << "," << r.tree.depth << "," << ratio << ","
         << setprecision(2) << (N ? 100.0 * (1.0 - (double)r.file.size() / N) : 0) << ","
         << setprecision(3) << medianMs(tb) << "," << medianMs(te) << "," << medianMs(td) << ","
         << (isPrefixFree(r.tree) ? "yes" : "no") << "," << setprecision(6) << kraftSum(r.tree) << ","
         << (ok ? "yes" : "no") << "\n";
    cout.unsetf(ios::fixed);
}

void printCodes(const string& path) {
    Bytes data = readFile(path);
    FreqTable f = countFrequencies(data);
    CodeTree t = buildTree(f);
    cout << "symbol,frequency,code,length\n";
    for (const Symbol& s : sortSymbols(f))
        cout << (int)s.sym << "," << s.freq << "," << t.code[s.sym] << "," << t.code[s.sym].size() << "\n";
}

int main(int argc, char** argv) {
    try {
        string cmd = argc > 1 ? argv[1] : "";
        if (cmd == "test") return runTests();
        if (cmd == "compress" && argc == 4) {
            CompressResult r = compressBytes(readFile(argv[2]));
            writeFile(argv[3], r.file);
            cerr << "wrote " << r.file.size() << " bytes (" << r.headerBytes << " header + "
                 << r.payloadBits << " code bits)\n";
            return 0;
        }
        if (cmd == "decompress" && argc == 4) { writeFile(argv[3], decompressBytes(readFile(argv[2]))); return 0; }
        if (cmd == "stats" && argc >= 3) {
            int runs = 5; int i = 2;
            if (string(argv[2]) == "--runs" && argc >= 5) { runs = stoi(argv[3]); i = 4; }
            statsHeader();
            for (; i < argc; i++) statsRow(argv[i], runs);
            return 0;
        }
        if (cmd == "codes" && argc == 3) { printCodes(argv[2]); return 0; }
        cerr << "usage: sf test | compress <in> <out> | decompress <in> <out> | stats [--runs R] <files...> | codes <file>\n";
        return 2;
    } catch (const exception& e) {
        cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
