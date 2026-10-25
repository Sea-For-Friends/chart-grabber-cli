#!/usr/bin/env python3
"""T-0090: select the translation units a developer's static analysis must look at.

Usage: changed_units.py --base REV --kind nocmake|ninja --builddir DIR < candidate-units.txt

stdin: candidate src/*.cpp paths (one per line, relative to the repo root, already module-scoped).
stdout: the subset to analyse, one per line; the single word ALL when the changed set touches something that
changes the analysis of every unit (tidy config, build files, the gate scripts) or when the base cannot be
resolved -- the caller then analyses everything, exactly like the landing gate.

Subset = candidates that are changed against REV (committed, staged, unstaged or untracked)
       + candidates whose last compile recorded a dependency on a changed file (direct or indirect include):
         nocmake -> build/nocmake*/obj/*.d (gcc -MMD), ninja -> `ninja -t deps` (gcc deps log).
A candidate with no recorded dependencies (never built) is always selected: unknown means analyse.
"""
import argparse, os, subprocess, sys

# Anything that changes how every unit is compiled or analysed selects everything: tidy/format config, build files,
# the gate scripts (tools/), and third_party/ (included with -isystem, so absent from the dependency files).
ALL_NAMES = (".clang-tidy", ".clang-format", "CMakeLists.txt", "CMakePresets.json")
ALL_PREFIXES = ("tools/", "third_party/", "cmake/")


def triggers_all(path):
    return os.path.basename(path) in ALL_NAMES or path.startswith(ALL_PREFIXES)


def git(*a):
    return subprocess.run(["git", *a], capture_output=True, text=True)


def changed_files(base):
    r = git("diff", "--name-only", "--diff-filter=ACMR", base, "--")
    if r.returncode != 0:
        return None
    files = set(r.stdout.split())
    files |= set(git("ls-files", "-o", "--exclude-standard").stdout.split())
    return files


def norm(p, root, base=None):
    """Repo-relative form of p; a relative p is relative to `base` (default: the repo root)."""
    p = os.path.normpath(p if os.path.isabs(p) else os.path.join(base or root, p))
    return os.path.relpath(p, root)


def deps_nocmake(builddir, root):
    """source -> set(files it depends on), from the -MMD files; the first prerequisite is the source itself."""
    out = {}
    od = os.path.join(builddir, "obj")
    if not os.path.isdir(od):
        return out
    for f in os.listdir(od):
        if f.endswith(".d"):
            toks = open(os.path.join(od, f), encoding="utf-8", errors="replace").read().replace("\\\n", " ").split()
            deps = [norm(t, root) for t in toks[1:] if not t.endswith(":")]
            if deps:
                out[deps[0]] = set(deps)
    return out


def deps_ninja(builddir, root):
    """Same shape from ninja's gcc deps log (`ninja -t deps`); first listed dependency is the source."""
    out = {}
    r = subprocess.run(["ninja", "-C", builddir, "-t", "deps"], capture_output=True, text=True)
    cur = []
    for line in r.stdout.splitlines() + [""]:
        if line.strip() and line[0].isspace():
            cur.append(norm(line.strip(), root, os.path.abspath(builddir)))  # ninja prints depfile paths relative to the build dir
        else:
            if cur:
                out[cur[0]] = set(cur)
            cur = []
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--base", required=True)
    ap.add_argument("--kind", choices=("nocmake", "ninja"), required=True)
    ap.add_argument("--builddir", required=True)
    a = ap.parse_args()
    root = os.getcwd()
    cands = [l.strip() for l in sys.stdin if l.strip()]
    ch = changed_files(a.base)
    if ch is None or any(triggers_all(c) for c in ch):
        print("ALL")
        return
    deps = (deps_nocmake if a.kind == "nocmake" else deps_ninja)(a.builddir, root)
    sel = [s for s in cands if s in ch or s not in deps or deps[s] & ch]
    print("\n".join(sel))


main()
