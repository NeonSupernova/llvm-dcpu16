#!/usr/bin/env python3
"""Build the modern backend, optionally bootstrapping pinned upstream LLVM."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import urllib.request

ROOT = Path(__file__).resolve().parents[1]


def run(*args):
    print("+", " ".join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), check=True)


def digest(path):
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(chunk)
    return result.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bootstrap", action="store_true", help="build the pinned LLVM source first")
    parser.add_argument("--llvm-dir", type=Path, help="existing LLVM CMake package directory")
    parser.add_argument("--work-dir", type=Path, default=ROOT.parent / "build-modern")
    parser.add_argument("--jobs", type=int, default=min(os.cpu_count() or 2, 6))
    parser.add_argument("--clang", type=Path, help="optional Clang with MSP430 frontend support")
    parser.add_argument("--skip-tests", action="store_true")
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    if args.bootstrap and args.llvm_dir:
        parser.error("choose --bootstrap or --llvm-dir")
    work = args.work_dir.resolve()
    work.mkdir(parents=True, exist_ok=True)
    llvm_dir = args.llvm_dir
    if args.bootstrap:
        lock = json.loads((ROOT / "llvm-source.json").read_text())
        archive = work / ("llvm-project-" + lock["version"] + ".src.tar.xz")
        if not archive.exists():
            partial = archive.with_suffix(archive.suffix + ".partial")
            print("Downloading", lock["url"], flush=True)
            urllib.request.urlretrieve(lock["url"], partial)
            if digest(partial) != lock["sha256"]:
                raise ValueError("LLVM source checksum mismatch")
            partial.replace(archive)
        if digest(archive) != lock["sha256"]:
            raise ValueError("cached LLVM source checksum mismatch")
        prefix = "llvm-project-" + lock["version"] + ".src"
        source = work / prefix
        if not (source / ".dcpu16-extracted").exists():
            run("tar", "-xf", archive, "-C", work,
                *(prefix + "/" + part for part in ("llvm", "cmake", "third-party", "libc")))
            (source / ".dcpu16-extracted").write_text(lock["sha256"] + "\n")
        llvm_build = work / "llvm-build"
        run("cmake", "-S", source / "llvm", "-B", llvm_build, "-G", "Ninja",
            "-DCMAKE_BUILD_TYPE=Release", "-DLLVM_TARGETS_TO_BUILD=",
            "-DLLVM_BUILD_LLVM_DYLIB=ON", "-DLLVM_LINK_LLVM_DYLIB=ON",
            "-DLLVM_ENABLE_ASSERTIONS=ON", "-DLLVM_INCLUDE_TESTS=OFF",
            "-DLLVM_INCLUDE_EXAMPLES=OFF", "-DLLVM_INCLUDE_BENCHMARKS=OFF",
            "-DLLVM_ENABLE_BINDINGS=OFF", "-DLLVM_ENABLE_ZSTD=OFF",
            "-DLLVM_ENABLE_ZLIB=OFF", "-DLLVM_ENABLE_LIBXML2=OFF",
            "-DPython3_EXECUTABLE=" + sys.executable)
        run("cmake", "--build", llvm_build, "--target", "LLVM", "llvm-tblgen", "-j", args.jobs)
        llvm_dir = llvm_build / "lib/cmake/llvm"
    if llvm_dir is None:
        config = shutil.which("llvm-config")
        if config:
            llvm_dir = Path(subprocess.check_output([config, "--cmakedir"], text=True).strip())
        else:
            parser.error("supply --llvm-dir or --bootstrap (llvm-config was not found)")
    build = work / "dcpu16-build"
    options = ["-DPython3_EXECUTABLE=" + sys.executable]
    if args.clang:
        options.append("-DDCPU16_CLANG_EXECUTABLE=" + str(args.clang.resolve()))
    run("cmake", "-S", ROOT.parent, "-B", build, "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=Release", "-DLLVM_DIR=" + str(llvm_dir.resolve()), *options)
    run("cmake", "--build", build, "-j", args.jobs)
    if not args.skip_tests:
        run("ctest", "--test-dir", build, "--output-on-failure")
    print("Compiler:", build / "modern/dcpu16-llc")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        raise SystemExit(f"error: {error}")
