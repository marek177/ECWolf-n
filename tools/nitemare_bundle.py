#!/usr/bin/env python3
"""Fingerprint complete local bundles. No commercial data or universal build IDs."""
import argparse
import hashlib
import json
from pathlib import Path

REQUIRED = tuple(f'{role}.{ep}' for ep in range(1, 4)
                 for role in ('MAP', 'IMG', 'WALLS', 'OBJECTS'))
OPTIONAL = ('SND.DAT', 'UIF.DAT', 'ENDING.FLI', 'GAME.PAL')


def inventory(directory):
    files = {}
    for path in Path(directory).iterdir():
        name = path.name.upper()
        if name not in REQUIRED + OPTIONAL:
            continue
        if name in files:
            raise ValueError(f'ambiguous case-insensitive filename: {name}')
        if not path.is_file():
            raise ValueError(f'not a regular file: {name}')
        data = path.read_bytes()
        files[name] = {'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
    missing = set(REQUIRED) - files.keys()
    if missing:
        raise ValueError('missing files: ' + ', '.join(sorted(missing)))
    return files


def verify(directory, manifest):
    if manifest.get('schema') != 1 or not isinstance(manifest.get('files'), dict):
        raise ValueError('unsupported manifest')
    expected = manifest['files']
    if not set(REQUIRED) <= expected.keys() or set(expected) - set(REQUIRED + OPTIONAL):
        raise ValueError('manifest must bind all twelve required files; unknown roles forbidden')
    actual = inventory(directory)
    if actual != expected:
        changed = sorted(k for k in actual.keys() | expected.keys()
                         if actual.get(k) != expected.get(k))
        raise ValueError('bundle mismatch: ' + ', '.join(changed))
    return actual


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument('--verify', type=Path, metavar='TRUSTED_MANIFEST')
    action.add_argument('--snapshot', type=Path, metavar='NEW_MANIFEST')
    parser.add_argument('--label', default='user-attested bundle; historical provenance unverified')
    args = parser.parse_args()
    try:
        if args.verify:
            verify(args.directory, json.loads(args.verify.read_text()))
            print('Exact bundle matches manifest. Historical provenance depends on manifest trust.')
        else:
            manifest = {'schema': 1, 'label': args.label, 'files': inventory(args.directory)}
            # Never silently replace a prior attestation with a new mixed bundle.
            with args.snapshot.open('x') as output:
                json.dump(manifest, output, indent=2, sort_keys=True)
                output.write('\n')
            print('Snapshot created; this records bytes, not proof of original distribution provenance.')
    except (ValueError, OSError) as error:
        parser.exit(1, f'{error}\n')


if __name__ == '__main__':
    main()
