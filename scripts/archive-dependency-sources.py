"""Bundle exact cached vcpkg sources and the recipes used for the local release.

Run after building, with the same vcpkg checkout and package cache. No binary
packages or launcher profiles are included. The matching full Qt source is a
separate unchanged upstream archive on the release.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tarfile
import tempfile

from chroma_version import read_version


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def registry_caches(explicit, root):
    if explicit is not None:
        return [explicit]
    candidates = []
    if os.environ.get('X_VCPKG_REGISTRIES_CACHE'):
        candidates.append(Path(os.environ['X_VCPKG_REGISTRIES_CACHE']) / 'git-trees')
    candidates.append(root / '.tools/vcpkg-registries-cache/git-trees')
    if os.environ.get('LOCALAPPDATA'):
        candidates.append(Path(os.environ['LOCALAPPDATA']) / 'vcpkg/registries/git-trees')
    return list(dict.fromkeys(path.resolve() for path in candidates))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    root = Path(__file__).resolve().parent.parent
    parser.add_argument('--version', help='Must match the application version; defaults to that version')
    parser.add_argument('--output', type=Path)
    parser.add_argument(
        '--registry-cache', type=Path,
        help='Use only this registry git-trees cache; defaults search environment, project, and LOCALAPPDATA caches',
    )
    args = parser.parse_args()
    version = read_version((root / 'CMakeLists.txt').read_text())
    if args.version and args.version != version:
        parser.error(f'Requested version differs from application version {version}')
    args.version = version
    tools = root / '.tools'
    vcpkg = tools / 'vcpkg'
    status = tools / 'vcpkg-installed/vcpkg/status'
    packages = sorted({line.removeprefix('Package: ') for line in status.read_text().splitlines() if line.startswith('Package: ')})
    archive = (args.output or root / f'dist/release/v{version}/Chroma-{version}-dependency-sources.tar.gz').resolve()
    if archive.exists():
        parser.error('Output already exists; choose a new archive path')
    archive.parent.mkdir(parents=True, exist_ok=True)
    prefix = f'Chroma-{args.version}-dependency-sources'
    registry_ports = [
        port
        for cache in registry_caches(args.registry_cache, root)
        if cache.is_dir()
        for port in sorted(cache.iterdir())
        if port.is_dir()
    ]
    inventory = []
    source_archives = sorted((tools / 'vcpkg-downloads').glob('*.tar.gz'))
    if len(source_archives) < 13:
        raise RuntimeError('Expected all 13 cached dependency/build-tool source archives')

    # Publish only a complete archive. A missing recipe or provenance file must
    # not leave a partial release asset that blocks the next attempt.
    with tempfile.TemporaryDirectory(prefix='chroma-dependency-', dir=archive.parent) as temporary:
        staged_archive = Path(temporary) / archive.name
        with tarfile.open(staged_archive, 'w:gz', compresslevel=9) as tar:
            def add(path, relative):
                tar.add(path, arcname=f'{prefix}/{relative}', recursive=True)

            for path in source_archives:
                add(path, f'downloads/{path.name}')
                inventory.append({'file': f'downloads/{path.name}', 'sha256': sha256(path), 'size': path.stat().st_size})
            for package in packages:
                abi = vcpkg / f'buildtrees/{package}/x64-windows.vcpkg_abi_info.txt'
                if not abi.exists():
                    raise RuntimeError(f'Missing build provenance: {abi}')
                expected = dict(line.split(' ', 1) for line in abi.read_text().splitlines() if ' ' in line).get('portfile.cmake')
                candidates = [root / f'cmake/vcpkg-ports/{package}', vcpkg / f'ports/{package}', *registry_ports]
                port = next((p for p in candidates if (p / 'portfile.cmake').is_file() and sha256(p / 'portfile.cmake') == expected), None)
                if port is None:
                    raise RuntimeError(f'Could not find exact installed port for {package} ({expected})')
                add(port, f'ports/{package}')
                add(abi, f'provenance/{package}-abi.txt')
                share = tools / f'vcpkg-installed/x64-windows/share/{package}'
                for filename in ('vcpkg.spdx.json', 'vcpkg-spdx-resources.json', 'copyright'):
                    if (share / filename).is_file():
                        add(share / filename, f'provenance/{package}/{filename}')
            # Build helpers and triplets from the checkout that actually built these DLLs.
            for folder in ('scripts', 'triplets', 'toolsrc'):
                add(vcpkg / folder, f'vcpkg/{folder}')
            for name in ('LICENSE.txt', 'NOTICE.txt', 'bootstrap-vcpkg.bat', 'bootstrap-vcpkg.sh'):
                if (vcpkg / name).is_file():
                    add(vcpkg / name, f'vcpkg/{name}')
            add(status, 'provenance/installed-status.txt')
            for path in ('vcpkg.json', 'vcpkg-configuration.json'):
                add(root / path, path)
            for folder in ('cmake/vcpkg-ports', 'cmake/vcpkg-triplets'):
                add(root / folder, folder)
            commit = subprocess.check_output(['git', '-c', f'safe.directory={vcpkg.as_posix()}', '-C', str(vcpkg), 'rev-parse', 'HEAD'], text=True).strip()
            metadata = {
                'vcpkgCheckoutCommit': commit, 'targetTriplet': 'x64-windows',
                'compiler': 'MSVC 19.35.32215.0', 'packages': packages, 'archives': inventory,
                'qtSource': 'qt-everywhere-src-6.5.3.tar.xz (separate release asset)',
            }
            data = json.dumps(metadata, indent=2).encode()
            import io
            info = tarfile.TarInfo(f'{prefix}/SOURCE-MANIFEST.json')
            info.size = len(data)
            tar.addfile(info, io.BytesIO(data))
            readme = f'''Exact dependency sources for Chroma {version} Windows x64.

downloads/ contains original upstream source archives. ports/ contains the exact
vcpkg recipes and patches matched against the installed package ABI metadata.
provenance/ includes package SPDX records, source URLs/checksums, and ABI records.
vcpkg/ includes build helpers, triplets, and tool source from the recorded checkout.
The Qt 6.5.3 full source is a separate unchanged release asset. Chroma and its
bundled libraries/submodules are in Chroma-{version}-source.tar.gz.

Use the application source docs/CHROMA.md build instructions. Copy downloads/
to VCPKG_DOWNLOADS; the manifest baseline selects these dependency versions.
The supplied ports can also be passed as VCPKG_OVERLAY_PORTS for these exact
recipes. Microsoft compiler/SDK/runtime are separate system toolchain components
under Microsoft terms, not represented as open source in this archive.
'''.encode('utf-8')
            info = tarfile.TarInfo(f'{prefix}/README.txt')
            info.size = len(readme)
            tar.addfile(info, io.BytesIO(readme))
        staged_archive.rename(archive)
    print(f'{archive}\nSHA256 {sha256(archive)}\n{archive.stat().st_size} bytes')


if __name__ == '__main__':
    main()
