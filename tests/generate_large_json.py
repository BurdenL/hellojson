"""Generate a real (not sparse) bounded-memory JSON fixture.

Example: python tests/generate_large_json.py out/fixtures/array-1g.json --mib 1024
"""
import argparse
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("output", type=Path)
parser.add_argument("--mib", type=int, required=True)
parser.add_argument("--shape", choices=("array", "nested", "string", "invalid", "truncated", "deep"), default="array")
args = parser.parse_args()
if args.mib <= 0:
    parser.error("--mib must be positive")
target = args.mib * 1024 * 1024
args.output.parent.mkdir(parents=True, exist_ok=True)
if args.shape in ("string", "deep"):
    opening, closing = (b"[" * 513 + b'"', b'"' + b"]" * 513) if args.shape == "deep" else (b'"', b'"')
    unit = "文本😀abc".encode("utf-8")
else:
    opening, closing = b"[", b"]"
    if args.shape == "nested":
        unit = '{"a":{"b":{"c":{"d":{"name":"中文😀","n":12345678901234567890}}}}},'.encode("utf-8")
    else:
        unit = '{"id":12345678901234567890,"name":"中文😀","enabled":true,"value":null},\n'.encode("utf-8")
count = (target - len(opening) - len(closing) - 2) // len(unit)
chunk_count = max(1, 1024 * 1024 // len(unit))
chunk = unit * chunk_count
with args.output.open("wb") as out:
    out.write(opening)
    remaining = count
    while remaining >= chunk_count:
        out.write(chunk)
        remaining -= chunk_count
    out.write(unit * remaining)
    if args.shape not in ("string", "deep"):
        out.write(b"0")  # consumes the last comma
    out.write(b" " if args.shape == "truncated" else b"!" if args.shape == "invalid" else closing)
    out.write(b" " * (target - out.tell()))
print(f"{args.output}: {target} bytes, shape={args.shape}")
