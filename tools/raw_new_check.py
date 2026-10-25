#!/usr/bin/env python3
"""T-0090: RAW-NEW hygiene check (same rule as before, minus false positives from comments).

Usage: raw_new_check.py FILE...   prints "FILE:LINE" for every offending line, exit 0; exit 2 on a usage/IO error
(the caller treats a non-zero exit as a gate failure -- fail closed).

Rule (unchanged from the former grep): a line matching  \\bnew\\s+[A-Za-z_:<>]+\\s*[\\(\\{]  is a finding unless the
ORIGINAL line also contains  new\\s+Q[A-Z]  (Qt types with a parent are exempt).
The only difference: text inside comments is ignored. Comments are found with a small lexer that knows string
literals, raw strings and character literals, so "//" or "/*" inside a string, and a line that merely starts with '*'
in code, never hide code. Strings are NOT blanked (the old grep looked into them and so does this).
"""
import re, sys

PAT = re.compile(r'\bnew\s+[A-Za-z_:<>]+\s*[\(\{]')
EXEMPT = re.compile(r'new\s+Q[A-Z]')


def scan_quoted(line, i, q):
    """line[i] is the opening quote q. Returns (end, continued): end = index after the closing quote (or len(line)),
    continued = True when the literal is still open because the line ends in a backslash (line splice)."""
    j, n = i + 1, len(line)
    while j < n:
        if line[j] == '\\':
            if j + 1 >= n:
                return n, True
            j += 2
        elif line[j] == q:
            return j + 1, False
        else:
            j += 1
    return n, False


def char_literal_start(line, i):
    """True when the quote at line[i] opens a character literal, False when it is a digit separator (1'000)."""
    j = i
    while j > 0 and (line[j - 1].isalnum() or line[j - 1] == '_'):
        j -= 1
    prefix = line[j:i]
    if prefix in ('', 'L', 'u', 'U', 'u8'):
        return True
    return not prefix[0].isdigit()  # a token starting with a digit is a number; anything else: be safe, treat as literal


def blank_comments(lines):
    out = []
    in_block = False
    raw_end = None   # closing sequence of an open raw string literal
    in_quote = None  # quote char of a string/char literal continued by backslash-newline
    for line in lines:
        res = []
        i, n = 0, len(line)
        while i < n:
            c = line[i]
            if in_quote is not None:
                # continuation line of a spliced literal: scan it as the rest of that literal
                end, cont = scan_quoted('"' + line[i:] if in_quote == '"' else "'" + line[i:], 0, in_quote)
                end -= 1
                res.append(line[i:i + end]); i += end
                in_quote = in_quote if cont else None
                continue
            if raw_end is not None:
                j = line.find(raw_end, i)
                if j < 0:
                    res.append(line[i:]); i = n
                else:
                    res.append(line[i:j + len(raw_end)]); i = j + len(raw_end); raw_end = None
                continue
            if in_block:
                j = line.find('*/', i)
                if j < 0:
                    res.append(' ' * (n - i)); i = n
                else:
                    res.append(' ' * (j + 2 - i)); i = j + 2; in_block = False
                continue
            two = line[i:i + 2]
            if two == '//':
                # a trailing backslash would continue the comment on the next line; that line is then treated as
                # code (flagging more, never less)
                res.append(' ' * (n - i)); i = n
            elif two == '/*':
                in_block = True; res.append('  '); i += 2
            elif c == '"':
                m = re.match(r'R"([^()\\ ]{0,16})\(', line[i - 1:i + 20]) if i > 0 and line[i - 1] == 'R' else None
                if m:
                    raw_end = ')' + m.group(1) + '"'
                    res.append(line[i:i + len(m.group(0)) - 1]); i += len(m.group(0)) - 1
                    continue
                end, cont = scan_quoted(line, i, '"')
                res.append(line[i:end]); i = end
                if cont:
                    in_quote = '"'
            elif c == "'" and char_literal_start(line, i):
                end, cont = scan_quoted(line, i, "'")
                res.append(line[i:end]); i = end
                if cont:
                    in_quote = "'"
            else:
                res.append(c); i += 1
        out.append(''.join(res))
    return out


def findings(lines):
    """1-based numbers of offending lines."""
    return [k for k, (orig, code) in enumerate(zip(lines, blank_comments(lines)), 1)
            if PAT.search(code) and not EXEMPT.search(orig)]


# (source text, expected flagged lines). The old grep flagged every line that is not entirely inside a comment;
# the checker may only differ from it by ignoring comment text, and must err towards flagging when unsure.
SELF_TESTS = [
    ("auto w = L'\"'; g(\"x//y\"); new T(1);", [1]),
    ("auto w = u'\"'; g(\"x//y\"); new T(1);", [1]),
    ("auto w = u8'\"'; g(\"x//y\"); new T(1);", [1]),
    ("auto w = U'\"'; g(\"x//y\"); new T(1);", [1]),
    ("char c = '\"'; g(\"x//y\"); new T(1);", [1]),
    ("f(\"\\\"//\"); new T(1);", [1]),
    ("f(\"\\\\\"); new T(1);", [1]),
    ("f(\"\\\\\"); // new T(1)", []),
    ("/* c */ new T(1);", [1]),
    ("/* a\n b */ new T(2);", [2]),
    ("/*/ x */ new T(1);", [1]),
    ("/*/ new T(1) */", []),
    ("s(\"abc\\\n//x\"); new T(2);", [2]),
    ("auto s = u8R\"x(\n// new T(1)\n)x\"; new T(2);", [2, 3]),
    ("int n = 1'000; // new T(1)", []),
    ("int n = 1'000; new T(1);", [1]),
    ("// new T(1)", []),
    ("/* new T(1) */", []),
    ("// c \\\nnew T(2);", [2]),
    ("*q = new T(1);", [1]),
    ("x(\"a//b\"); auto* b = new T(3);", [1]),
    ("y(\"/*\"); new T(4);", [1]),
    ("foo(); // new idea(5)", []),
    ("auto* w = new QObject(this);", []),
    ("auto* w = new T(1); // new QObject(this)", []),  # exemption is per original line, exactly as the old grep
]


def self_test():
    bad = 0
    for src, want in SELF_TESTS:
        got = findings(src.split('\n'))
        if got != want:
            bad += 1
            print(f"raw_new_check self-test FAILED: {src!r}: expected {want}, got {got}", file=sys.stderr)
    return 1 if bad else 0


def main():
    if sys.argv[1:] == ['--self-test']:
        return self_test()
    for path in sys.argv[1:]:
        try:
            lines = open(path, encoding='utf-8', errors='replace').read().split('\n')
        except OSError as e:
            print(f"raw_new_check: {e}", file=sys.stderr); return 2
        for k in findings(lines):
            print(f"{path}:{k}")
    return 0


sys.exit(main())
