#!/usr/bin/env python3
"""Generator of the 10-byte x87 literals used by tests/marks/extended80_test.cpp and geometry_sml_test.cpp (T-0109).
Run: python3 -I tools/gen_sml_vectors.py   (prints "value  bytes" in memory order: 8 bytes significand LE, 2 bytes sign+exponent LE).
Independent of the C++ code: struct for the double bits, integer arithmetic for the widening."""
import struct

def ext(v):
    b = struct.unpack('<Q', struct.pack('<d', v))[0]
    s, e, f = b >> 63, (b >> 52) & 0x7FF, b & ((1 << 52) - 1)
    if e == 0 and f == 0:
        sig, ee = 0, 0
    elif e == 0:
        lz = 64 - f.bit_length()
        sig, ee = f << lz, 63 - lz - 1074 + 16383
    else:
        sig, ee = (1 << 63) | (f << 11), e - 1023 + 16383
    return (sig.to_bytes(8, 'little') + (ee | (s << 15)).to_bytes(2, 'little')).hex()

VALUES = [0.0, -0.0, 1.0, -1.0, 37.6173, -122.4194, 359.99999999, 5e-324, 1.7976931348623157e308, 10.0, 20.0, 30.0,
          40.0, 50.0, 60.0, 2.5, -3.5, 12.5, -45.25, 4.0, 180.0, 360.0, -360.0, 400.0, 0.1]

if __name__ == '__main__':
    for v in VALUES:
        print(repr(v), ext(v))
