#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2025-2026 ZioZoni95
"""Shingle-overlap scan: our sources against a reference corpus.

    PYTHONHASHSEED=0 python3 scripts/licence_code_scan.py duckstation_ref
    PYTHONHASHSEED=0 python3 scripts/licence_code_scan.py pcsx-redux

Run before a release. Comments and string literals are dropped, so what is
compared is code shape and identifier sequence — what survives reformatting but
not rewriting. Windows that are mostly punctuation are discarded, because a
12-token run of ') ; } if (' is shared by every C file ever written.

Reading the output: a file at a few tenths of a percent is noise. A file with
hundreds of hits against one specific reference file is a lead — take it to
scripts/licence_prose_scan.py and to the two files side by side. Constant tables
transcribed from the hardware documentation legitimately match every emulator's
copy of the same table; that is a fact, not a derivation, as long as the code
cites where the numbers come from.
"""
import os, re, sys

TOKEN = re.compile(r"[A-Za-z_][A-Za-z0-9_]*|0[xX][0-9a-fA-F]+|\d+|[^\s\w]")
K = 12          # shingle length in tokens
SAMPLE = 4      # keep 1 in SAMPLE hashes (consistent on both sides)

def strip_code(src):
    src = re.sub(r"/\*.*?\*/", " ", src, flags=re.S)
    src = re.sub(r"//[^\n]*", " ", src)
    src = re.sub(r'"(\\.|[^"\\])*"', ' "" ', src)
    src = re.sub(r"'(\\.|[^'\\])*'", " '' ", src)
    return src

def tokens(path):
    try:
        with open(path, "r", encoding="utf-8", errors="ignore") as f:
            return TOKEN.findall(strip_code(f.read()))
    except OSError:
        return []

IDENT = re.compile(r"^[A-Za-z_][A-Za-z0-9_]{2,}$")

def meaningful(win):
    """Reject windows that are mostly punctuation/keywords: a 12-token run of
    ') ; } if (' is shared by every C file ever written and says nothing."""
    names = {t for t in win if IDENT.match(t)}
    return len(names) >= 5

def shingles(toks):
    for i in range(len(toks) - K + 1):
        win = toks[i:i + K]
        if not meaningful(win):
            continue
        h = hash(" ".join(win)) & 0xFFFFFFFFFFFF
        if h % SAMPLE == 0:
            yield h, i

def walk(root, exts):
    for dirpath, dirnames, files in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in (".git", "build", "third_party", "deps")]
        for fn in files:
            if os.path.splitext(fn)[1] in exts:
                yield os.path.join(dirpath, fn)

ref_root = sys.argv[1]
ref_index = {}
nref = 0
for path in walk(ref_root, {".c", ".cc", ".cpp", ".h", ".hh", ".hpp", ".inl"}):
    toks = tokens(path)
    if len(toks) < K:
        continue
    nref += 1
    for h, _ in shingles(toks):
        ref_index.setdefault(h, path)
print(f"reference: {nref} files, {len(ref_index)} sampled shingles", file=sys.stderr)

ours = list(walk("src", {".c", ".cpp"})) + list(walk("include", {".h"}))
rows = []
for path in ours:
    toks = tokens(path)
    if len(toks) < K:
        continue
    hits, total, where = 0, 0, {}
    for h, i in shingles(toks):
        total += 1
        if h in ref_index:
            hits += 1
            where.setdefault(ref_index[h], []).append(i)
    if total:
        rows.append((hits / total, hits, total, path, where))

rows.sort(reverse=True)
for frac, hits, total, path, where in rows:
    if hits == 0:
        continue
    top = sorted(where.items(), key=lambda kv: -len(kv[1]))[:3]
    print(f"{frac*100:6.2f}%  {hits:4d}/{total:5d}  {path}")
    for ref, idxs in top:
        print(f"            {len(idxs):4d}  {ref}")
