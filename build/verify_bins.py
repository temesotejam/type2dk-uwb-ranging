#!/usr/bin/env python3
"""Verify Type2DK boot headers and the exact three firmware files in a manifest."""
import argparse
import binascii
import hashlib
import json
from pathlib import Path
import struct


def verify_image(path):
    data = path.read_bytes()
    if len(data) < 76:
        raise ValueError('Image too short: ' + str(path))
    h = struct.unpack_from('<11I', data)
    checks = [
        sum(h[:8]) & 0xffffffff == 0,
        h[8] == 0x98447902,
        binascii.crc32(data[:40]) & 0xffffffff == h[10],
        0x04000400 <= h[0] < 0x04016000 and h[0] % 8 == 0,
        h[1] & 1 and (h[1] & ~1) < h[9],
        h[9] + 32 == len(data),
        b'TYPE2DK_DIRECT_RANGE_V1' in data,
        all(x not in data for x in (b'TX_BEGIN,NODE=', b'TX_END,NODE=', b'D21_22=',
                                   b'TYPE2DK_MESH_ACCEL', b'TYPE2DK_PAIR_DATA_PROBE')),
    ]
    if not all(checks):
        raise ValueError('Invalid image header or application: ' + str(path))
    boot = struct.unpack_from('<8I', data, h[9])
    if not (boot[:3] == (0xbb0110bb, 0, 0) and boot[3] == len(data)
            and boot[4] >= len(data) and boot[4] % 8192 == 0 and boot[5:7] == (0, 0)):
        raise ValueError('Invalid boot trailer: ' + str(path))
    return {'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}


def verify_manifest(folder):
    manifest = json.loads((folder / 'manifest.json').read_text())
    rows = manifest['binaries']
    if sorted(r['node'] for r in rows) != [19, 21, 22]:
        raise ValueError('Manifest must contain exactly nodes 19, 21, 22')
    hashes = set()
    for row in rows:
        if Path(row['file']).name != row['file']:
            raise ValueError('Manifest paths must be plain filenames')
        actual = verify_image(folder / row['file'])
        if any(actual[k] != row[k] for k in actual):
            raise ValueError('Manifest mismatch: ' + row['file'])
        hashes.add(actual['sha256'])
        print('PASS node {}: {}'.format(row['node'], row['file']))
    if len(hashes) != 3:
        raise ValueError('Node binaries must differ')
    return manifest


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('folder', type=Path)
    verify_manifest(parser.parse_args().folder)
