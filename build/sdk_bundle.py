#!/usr/bin/env python3
"""Create a private dependency bundle, or retrieve it in GitHub Actions."""
import argparse
import os
from pathlib import Path
import shutil
import tempfile
import urllib.request
import zipfile
from prepare_sdk import SDK_SHA256, MURATA_SHA256, check_archive, prepare


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    subs = parser.add_subparsers(dest='command', required=True)
    pack = subs.add_parser('pack')
    pack.add_argument('--sdk-zip', required=True)
    pack.add_argument('--murata-zip', required=True)
    pack.add_argument('--out', required=True)
    fetch = subs.add_parser('fetch')
    fetch.add_argument('--out', required=True)
    args = parser.parse_args()
    if args.command == 'pack':
        check_archive(args.sdk_zip, SDK_SHA256)
        check_archive(args.murata_zip, MURATA_SHA256)
        with zipfile.ZipFile(args.out, 'x', compression=zipfile.ZIP_STORED) as archive:
            archive.write(args.sdk_zip, 'sdk.zip')
            archive.write(args.murata_zip, 'murata.zip')
        print('Created private SDK bundle. Do not commit or publish it: ' + args.out)
        return
    url = os.environ.get('SDK_DOWNLOAD_URL', '')
    if not url.startswith('https://'):
        raise SystemExit('Set the SDK_DOWNLOAD_URL Actions secret to an authorized HTTPS bundle URL. See docs/BUILD.md.')
    with tempfile.TemporaryDirectory() as temp:
        temp = Path(temp)
        # Never echo the signed URL or HTTP exception (which may contain it).
        try:
            with urllib.request.urlopen(url, timeout=120) as response, (temp / 'bundle.zip').open('wb') as output:
                if not response.url.startswith('https://'):
                    raise ValueError('HTTPS required')
                shutil.copyfileobj(response, output)
        except Exception:
            raise SystemExit('SDK download failed. Check the private URL and expiry in SDK_DOWNLOAD_URL.') from None
        with zipfile.ZipFile(temp / 'bundle.zip') as archive:
            for name in ('sdk.zip', 'murata.zip'):
                (temp / name).write_bytes(archive.read(name))
        prepare(temp / 'sdk.zip', temp / 'murata.zip', args.out)


if __name__ == '__main__':
    main()
