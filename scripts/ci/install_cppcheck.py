#!/usr/bin/env python3
"""Build the hash-pinned Cppcheck CLI locally, without Qt or upstream tests."""

import hashlib
import subprocess
from pathlib import Path

VERSION = '2.22.0'
ARCHIVE_SHA256 = 'd74945deb2d50393430e07596b766f8a779512c7f60dac2a30ea64e059ece57b'
URL = f'https://github.com/cppcheck-opensource/cppcheck/archive/refs/tags/{VERSION}.tar.gz'


def main():
    directory = Path(__file__).resolve().parents[2] / 'out/cppcheck-tool'
    directory.mkdir(parents=True, exist_ok=True)
    release = directory / VERSION
    release.mkdir(parents=True, exist_ok=True)
    archive = release / 'source.tar.gz'
    subprocess.run(['curl', '--fail', '--location', '--silent', '--show-error',
                    '--max-time', '120', URL, '--output', str(archive)], check=True)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != ARCHIVE_SHA256:
        raise SystemExit('Cppcheck source checksum mismatch')
    subprocess.run(['tar', '-xzf', str(archive), '-C', str(release)], check=True)
    build = release / 'build'
    install = directory / 'install'
    subprocess.run(['cmake', '-S', str(release / f'cppcheck-{VERSION}'), '-B', str(build),
                    '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DBUILD_GUI=OFF',
                    '-DBUILD_TESTS=OFF', '-DUSE_MATCHCOMPILER=Off',
                    f'-DCMAKE_INSTALL_PREFIX={install}'], check=True)
    subprocess.run(['cmake', '--build', str(build), '--target', 'cppcheck', '-j2'], check=True)
    subprocess.run(['cmake', '--install', str(build)], check=True)
    subprocess.run([str(install / 'bin/cppcheck'), '--version'], check=True)


if __name__ == '__main__':
    main()
