#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2025-2026 ZioZoni95
"""Do any of our comment sentences appear verbatim in a reference tree?

    python3 scripts/licence_prose_scan.py duckstation_ref
    python3 scripts/licence_prose_scan.py pcsx-redux

Prose is the strongest plagiarism signal there is: two people implementing the
same register never write the same sentence about it by accident. Expect hits
on MIT licence boilerplate — that text is identical everywhere by design, and
has to be present. Anything else is a finding.
"""
import os, re, sys

def comments(path):
    txt = open(path, encoding="utf-8", errors="ignore").read()
    out = []
    for m in re.finditer(r"/\*.*?\*/|//[^\n]*", txt, re.S):
        body = re.sub(r"[/*]", " ", m.group(0))
        for sent in re.split(r"[.;\n]", body):
            words = re.findall(r"[A-Za-z][A-Za-z'-]+", sent)
            if len(words) >= 7:
                out.append((" ".join(w.lower() for w in words), txt[:m.start()].count("\n") + 1))
    return out

def walk(root, exts):
    for dirpath, dirnames, files in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in (".git", "build")]
        for fn in files:
            if os.path.splitext(fn)[1] in exts:
                yield os.path.join(dirpath, fn)

ref_root = sys.argv[1]
ref = {}
for p in walk(ref_root, {".c", ".cc", ".cpp", ".h", ".hh", ".hpp", ".inl"}):
    for sent, line in comments(p):
        ref.setdefault(sent, (p, line))
print(f"reference sentences: {len(ref)}", file=sys.stderr)

hits = 0
for p in list(walk("src", {".c", ".cpp"})) + list(walk("include", {".h"})):
    for sent, line in comments(p):
        if sent in ref:
            rp, rl = ref[sent]
            hits += 1
            print(f"{p}:{line}  <->  {rp}:{rl}\n    {sent[:120]}")
print(f"{hits} verbatim comment sentences")
