"""Fetch (if absent), hash-check and safely unpack the locked upstream archives."""
import hashlib
import json
import tarfile
import urllib.request
from pathlib import Path

root = Path(__file__).resolve().parent
lock = json.loads((root / 'source-lock.json').read_text())
archives = root / 'sources'
src = root / 'build/src'
archives.mkdir(exist_ok=True)
src.mkdir(parents=True, exist_ok=True)
for name, metadata in lock.items():
    archive = archives / (name + '.tar.gz')
    if not archive.exists():
        temporary = archive.with_suffix('.download')
        urllib.request.urlretrieve(metadata['url'], temporary)
        if hashlib.sha256(temporary.read_bytes()).hexdigest() != metadata['sha256']:
            raise RuntimeError('Upstream archive hash mismatch: ' + name)
        temporary.replace(archive)
    if archive.stat().st_size != metadata['bytes'] or hashlib.sha256(archive.read_bytes()).hexdigest() != metadata['sha256']:
        raise RuntimeError('Archive differs from source-lock.json: ' + name)
    destination = src / name
    if destination.exists():
        print('Verified archive; reusing build source tree:', destination)
        continue
    temporary = src / ('unpack-' + name)
    temporary.mkdir()
    with tarfile.open(archive) as tar:
        tar.extractall(temporary, filter='data')
    children = list(temporary.iterdir())
    if len(children) != 1 or not children[0].is_dir():
        raise RuntimeError('Unexpected upstream archive layout: ' + name)
    children[0].rename(destination)
    temporary.rmdir()
    print('Verified and extracted:', name, metadata['revision'])
