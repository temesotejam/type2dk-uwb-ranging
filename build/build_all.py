#!/usr/bin/env python3
"""Build and validate all three nodes, then assemble a flashable artifact."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
from verify_bins import verify_image, verify_manifest

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--sdk', required=True)
parser.add_argument('--gcc-bin', required=True)
parser.add_argument('--out', default='out')
args = parser.parse_args()
root = Path(__file__).resolve().parent.parent
out = Path(args.out).resolve()
out.mkdir(parents=True, exist_ok=True)
package = out / 'firmware'
package.mkdir(exist_ok=True)
rows = []
for node in (19, 21, 22):
    dest = out / f'node{node}'
    subprocess.run([sys.executable, str(root / 'build/build.py'), '--sdk', args.sdk,
                    '--gcc-bin', args.gcc_bin, '--node', str(node), '--out', str(dest)], check=True)
    filename = f'2dk_range_node{node}.bin'
    shutil.copyfile(dest / filename, package / filename)
    rows.append({'node': node, 'file': filename, **verify_image(package / filename)})
    symbols = subprocess.check_output([str(Path(args.gcc_bin).resolve() / 'arm-none-eabi-nm'),
                                      '--defined-only', str(dest / filename.replace('.bin', '.elf'))], text=True)
    if {line.split()[-1] for line in symbols.splitlines()} & {'UwbApi_SendData', 'send_state', 'on_data', 'mesh_accel_sample'}:
        raise SystemExit('Unexpected application data transfer or accelerometer symbol')
try:
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True, stderr=subprocess.DEVNULL).strip()
except subprocess.CalledProcessError:
    commit = 'uncommitted'
(package / 'manifest.json').write_text(json.dumps({'build': 'TYPE2DK_DIRECT_RANGE_V1',
                                                  'commit': commit, 'hardware_tested': False,
                                                  'binaries': rows}, indent=2) + '\n')
(package / 'SHA256SUMS.txt').write_text(''.join(f"{r['sha256']}  {r['file']}\n" for r in rows))
shutil.copyfile(root / 'scripts/flash_all.ps1', package / 'flash_all.ps1')
shutil.copytree(root / 'licenses', package / 'licenses', dirs_exist_ok=True)
shutil.copyfile(root / 'THIRD_PARTY_NOTICES.md', package / 'THIRD_PARTY_NOTICES.md')
verify_manifest(package)
print('Firmware artifact: ' + str(package))
