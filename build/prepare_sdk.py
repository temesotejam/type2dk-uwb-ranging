#!/usr/bin/env python3
"""Prepare the pinned, separately obtained NXP SDK and Murata board patch."""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile

SDK_SHA256 = 'e2f021fe1e59bf3c84bc9480599000a52ad079b5a24e23dc385cf650e68bb8dc'
MURATA_SHA256 = '9c233f6ed78f41afa84678f0511f44fec4581dfe61cf4b26d7037b554f433d71'


def check_archive(path, expected):
    if hashlib.sha256(Path(path).read_bytes()).hexdigest() != expected:
        raise ValueError('Unexpected archive SHA256: ' + str(path))


def prepare(sdk_zip, murata_zip, out):
    check_archive(sdk_zip, SDK_SHA256)
    check_archive(murata_zip, MURATA_SHA256)
    out = Path(out).resolve()
    if out.exists():
        raise ValueError('Use a new output directory: ' + str(out))
    out.mkdir(parents=True)
    with zipfile.ZipFile(sdk_zip) as archive:
        for entry in archive.infolist():
            name = entry.filename
            marker = '/uwbiot-top/'
            if marker not in name or entry.is_dir():
                continue
            target = (out / name.split(marker, 1)[1]).resolve()
            if not target.is_relative_to(out):
                raise ValueError('Invalid archive path')
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(archive.read(entry))
    with zipfile.ZipFile(murata_zip) as archive:
        matches = [n for n in archive.namelist() if n.endswith('/2dk_prebuilt_v04.03.14.patch')]
        if len(matches) != 1:
            raise ValueError('Expected exactly one Murata board patch')
        subprocess.run(['patch', '-p0', '--batch', '--forward'],
                       input=archive.read(matches[0]), cwd=out, check=True)
    subprocess.run([sys.executable, str(Path(__file__).with_name('apply_sdk_changes.py')),
                    str(out)], check=True)
    return out


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk-zip', required=True)
    parser.add_argument('--murata-zip', required=True)
    parser.add_argument('--out', required=True)
    args = parser.parse_args()
    print(prepare(args.sdk_zip, args.murata_zip, args.out))
