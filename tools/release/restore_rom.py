"""Rebuild the ROM from the NG1_ROM_0..7 secrets (see upload-rom-secret.sh).

    python tools/release/restore_rom.py ninjagaiden.nes [parts.b64]

The release workflow writes the secrets into a file (Windows limits one
environment variable to 32,767 characters, less than a part); without a file
the parts are read from NG1_ROM_0..7 environment variables. The result
must be Ninja Gaiden (USA); anything else stops the
build.
"""
import base64
import hashlib
import lzma
import os
import sys

# SHA-256 of the ROM after its 16-byte iNES header (the header varies by dump
# tool; the game data does not).
EXPECTED = "6799437d4122b81c86ae35cefe5b6ae6e10e6f9a9c7b3140dd569f717ba32b3d"


def main() -> int:
    out = sys.argv[1] if len(sys.argv) > 1 else "ninjagaiden.nes"
    if len(sys.argv) > 2:
        with open(sys.argv[2], encoding="ascii") as f:
            text = "".join(f.read().split())
    else:
        parts = [os.environ.get(f"NG1_ROM_{i}", "") for i in range(8)]
        text = "".join(p.strip() for p in parts)
    if not text:
        print("No NG1_ROM_* secrets: run tools/release/upload-rom-secret.sh first",
              file=sys.stderr)
        return 1
    rom = lzma.decompress(base64.b64decode(text))
    digest = hashlib.sha256(rom[16:]).hexdigest()
    if digest != EXPECTED:
        print(f"The stored ROM is not Ninja Gaiden (USA): {digest}", file=sys.stderr)
        return 1
    with open(out, "wb") as f:
        f.write(rom)
    print(f"ROM restored ({len(rom)} bytes, verified)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
