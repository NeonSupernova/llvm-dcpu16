#!/usr/bin/env python3
"""Build/test the isolated experimental LLVM-core addressable-unit patch.

This does not enable --byte-size=16 code generation. Use a separate LLVM source
copy and build directory; the patch changes LLVM's C++ ABI.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / 'llvm-core'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(*args, cwd=None):
    print('+', ' '.join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), cwd=cwd, check=True)


def prepare_source(source, series):
    states = []
    for name, hashes in series['files'].items():
        path = source / name
        if not path.is_file():
            raise ValueError(f'missing pinned LLVM source file: {path}')
        actual = digest(path)
        if actual == hashes['original_sha256']:
            states.append('original')
        elif actual == hashes['patched_sha256']:
            states.append('patched')
        else:
            raise ValueError(f'source hash mismatch; refusing to modify {path}')
    for record in series['patches']:
        patch = CORE / record['file']
        if digest(patch) != record['sha256']:
            raise ValueError(f'patch checksum mismatch: {patch}')
    if set(states) == {'patched'}:
        print('Exact patched source already present', flush=True)
        return
    if set(states) != {'original'}:
        raise ValueError('partially patched source; use a separate pristine source copy')
    for record in series['patches']:
        patch = CORE / record['file']
        run('git', 'apply', '--check', patch, cwd=source)
        run('git', 'apply', patch, cwd=source)
    for name, hashes in series['files'].items():
        if digest(source / name) != hashes['patched_sha256']:
            raise ValueError(f'patched source checksum mismatch: {name}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--llvm-source', type=Path, help='separate llvm-project source root (will be patched)')
    parser.add_argument('--bootstrap', action='store_true', help='download/extract the checksum-pinned LLVM source')
    parser.add_argument('--work-dir', type=Path, default=ROOT.parent / 'build-word-layout')
    parser.add_argument('--jobs', type=int, default=min(os.cpu_count() or 2, 6))
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    if args.bootstrap == bool(args.llvm_source):
        parser.error('choose exactly one of --bootstrap or --llvm-source')
    work = args.work_dir.resolve()
    work.mkdir(parents=True, exist_ok=True)
    series = json.loads((CORE / 'series.json').read_text())
    lock = json.loads((ROOT / 'llvm-source.json').read_text())
    if lock['version'] != series['llvm_version']:
        raise ValueError('core patch series does not match the pinned LLVM release')
    if args.bootstrap:
        archive = work / f"llvm-project-{lock['version']}.src.tar.xz"
        if not archive.exists():
            partial = archive.with_suffix(archive.suffix + '.partial')
            urllib.request.urlretrieve(lock['url'], partial)
            if digest(partial) != lock['sha256']:
                raise ValueError('LLVM archive checksum mismatch')
            partial.replace(archive)
        if digest(archive) != lock['sha256']:
            raise ValueError('cached LLVM archive checksum mismatch')
        prefix = f"llvm-project-{lock['version']}.src"
        source = work / prefix
        marker = source / '.dcpu16-word-source'
        if not marker.exists():
            # Never overwrite another checkout or a partially modified source.
            if source.exists():
                raise ValueError(f'unmarked source exists; use a fresh work directory: {source}')
            run('tar', '-xf', archive, '-C', work,
                *(prefix + '/' + part for part in ('llvm', 'cmake', 'third-party', 'libc')))
            marker.write_text(lock['sha256'] + '\n')
        elif marker.read_text().strip() != lock['sha256']:
            raise ValueError('source marker does not match pinned archive')
    else:
        source = args.llvm_source.resolve()
    prepare_source(source, series)
    build = work / 'llvm-core-build'
    run('cmake', '-S', source / 'llvm', '-B', build, '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Release', '-DLLVM_TARGETS_TO_BUILD=',
        '-DLLVM_ENABLE_ASSERTIONS=ON', '-DLLVM_BUILD_LLVM_DYLIB=OFF',
        '-DLLVM_LINK_LLVM_DYLIB=OFF', '-DLLVM_INCLUDE_TESTS=OFF',
        '-DLLVM_INCLUDE_EXAMPLES=OFF', '-DLLVM_INCLUDE_BENCHMARKS=OFF',
        '-DLLVM_ENABLE_ZSTD=OFF', '-DLLVM_ENABLE_ZLIB=OFF', '-DLLVM_ENABLE_LIBXML2=OFF',
        '-DPython3_EXECUTABLE=' + sys.executable)
    run('cmake', '--build', build, '--target', 'LLVMCore', 'LLVMAsmParser',
        'LLVMBitReader', 'LLVMBitWriter', 'LLVMTransformUtils', 'LLVMInterpreter', 'LLVMScalarOpts', 'LLVMInstCombine', '-j', args.jobs)
    tests = work / 'layout-tests'
    run('cmake', '-S', CORE / 'tests', '-B', tests, '-G', 'Ninja',
        '-DLLVM_DIR=' + str(build / 'lib/cmake/llvm'))
    run('cmake', '--build', tests, '-j', args.jobs)
    run('ctest', '--test-dir', tests, '--output-on-failure')


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        raise SystemExit(f'error: {error}')
