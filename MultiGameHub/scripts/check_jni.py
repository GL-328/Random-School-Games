#!/usr/bin/env python3
"""Verifies every JNI method looked up from C++ exists with the same descriptor
in the compiled PlatformBridge class (catches crashes that only show on device).
Usage: check_jni.py <dir-with-compiled-classes>"""
import os, re, subprocess, sys
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
out = subprocess.run(["javap", "-s", "-public", "-cp", sys.argv[1], "com.gl328.multigamehub.PlatformBridge"],
                     capture_output=True, text=True, check=True).stdout.splitlines()
java = {}
for a, b in zip(out, out[1:]):
    m, d = re.search(r"\s(\w+)\(", a), re.search(r"descriptor: (\S+)", b)
    if m and d and " static " in a:
        java.setdefault(m.group(1), set()).add(d.group(1))
src = open(os.path.join(root, "src/platform/android/AndroidPlatform.cpp")).read()
bad = [(n, s) for n, s in re.findall(r'method\("(\w+)",\s*"([^"]+)"\)', src) if s not in java.get(n, set())]
for n, s in bad:
    print("JNI MISMATCH:", n + s)
print("JNI check:", "OK" if not bad else f"{len(bad)} mismatches")
sys.exit(1 if bad else 0)
