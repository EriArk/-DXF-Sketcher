#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import shutil
import stat
import sys
import sysconfig
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parent.parent
PYVENDOR_SRC = REPO_ROOT / "3rd_party" / "pyvendor"


def ignore_pycache(_dir: str, names: list[str]) -> list[str]:
    ignored: list[str] = []
    for name in names:
        if name == "__pycache__" or name.endswith((".pyc", ".pyo")):
            ignored.append(name)
    return ignored


def copy_tree(src: Path, dst: Path) -> None:
    if dst.exists():
        shutil.rmtree(dst)
    shutil.copytree(src, dst, ignore=ignore_pycache, symlinks=True)


def copy_file(src: Path, dst: Path) -> None:
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)


def make_executable(path: Path) -> None:
    mode = path.stat().st_mode
    path.chmod(mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


def write_posix_wrapper(wrapper: Path, real_exe: str) -> None:
    wrapper.parent.mkdir(parents=True, exist_ok=True)
    wrapper.write_text(
        "#!/usr/bin/env sh\n"
        "set -eu\n"
        'root="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"\n'
        'export PYTHONHOME="$root"\n'
        'libpath="$root/lib"\n'
        'if [ -n "${LD_LIBRARY_PATH:-}" ]; then\n'
        '  export LD_LIBRARY_PATH="$libpath:$LD_LIBRARY_PATH"\n'
        "else\n"
        '  export LD_LIBRARY_PATH="$libpath"\n'
        "fi\n"
        'if [ -n "${DYLD_LIBRARY_PATH:-}" ]; then\n'
        '  export DYLD_LIBRARY_PATH="$libpath:$DYLD_LIBRARY_PATH"\n'
        "else\n"
        '  export DYLD_LIBRARY_PATH="$libpath"\n'
        "fi\n"
        f'exec "$root/{real_exe}" "$@"\n',
        encoding="utf-8",
    )
    make_executable(wrapper)


def find_posix_libpython() -> Path | None:
    version = f"{sys.version_info.major}.{sys.version_info.minor}"
    candidates: list[Path] = []

    for key in ("LDLIBRARY", "INSTSONAME"):
        value = sysconfig.get_config_var(key)
        if value:
            libname = Path(str(value)).name
            for base in (
                Path(sys.executable).resolve().parent,
                Path(str(sysconfig.get_config_var("LIBDIR") or "")),
                Path(sys.base_prefix) / "lib",
            ):
                if str(base):
                    candidates.append(base / libname)

    framework = sysconfig.get_config_var("PYTHONFRAMEWORK")
    framework_prefix = sysconfig.get_config_var("PYTHONFRAMEWORKPREFIX")
    if framework and framework_prefix:
        candidates.append(Path(framework_prefix) / f"{framework}.framework" / "Versions" / version / framework)

    seen: set[Path] = set()
    for candidate in candidates:
        if candidate in seen:
            continue
        seen.add(candidate)
        if candidate.exists():
            return candidate
    return None


def bundle_posix_python(dest_pyvendor: Path) -> None:
    version = f"{sys.version_info.major}.{sys.version_info.minor}"
    runtime_root = dest_pyvendor / "python"
    stdlib_src = Path(sysconfig.get_path("stdlib"))
    if not stdlib_src.exists():
        raise RuntimeError(f"stdlib not found: {stdlib_src}")

    libexec_dir = runtime_root / "libexec"
    real_exe = libexec_dir / "python3-real"
    copy_file(Path(sys.executable).resolve(), real_exe)
    make_executable(real_exe)

    stdlib_dst = runtime_root / "lib" / f"python{version}"
    copy_tree(stdlib_src, stdlib_dst)

    libpython = find_posix_libpython()
    if libpython:
        copy_file(libpython, runtime_root / "lib" / libpython.name)

    write_posix_wrapper(runtime_root / "bin" / "python3", "libexec/python3-real")
    write_posix_wrapper(runtime_root / "bin" / "python", "libexec/python3-real")


def find_windows_dlls(bin_dir: Path) -> list[Path]:
    version = f"{sys.version_info.major}.{sys.version_info.minor}"
    version_tag = f"{sys.version_info.major}{sys.version_info.minor}"
    search_roots = [
        bin_dir,
        Path(sys.base_prefix),
        Path(sys.base_prefix) / "bin",
        Path(sys.base_prefix) / "DLLs",
        Path(sys.base_prefix) / "lib",
    ]
    exact_candidates = (
        f"python{version_tag}.dll",
        f"libpython{version}.dll",
        f"libpython{version_tag}.dll",
    )

    found: list[Path] = []
    seen: set[Path] = set()
    for root in search_roots:
        if not root.exists():
            continue

        for name in exact_candidates:
            candidate = root / name
            if candidate.exists() and candidate not in seen:
                found.append(candidate)
                seen.add(candidate)

        for pattern in (f"*python{version_tag}*.dll", f"*python{version}*.dll"):
            for candidate in sorted(root.glob(pattern)):
                if candidate.exists() and candidate not in seen:
                    found.append(candidate)
                    seen.add(candidate)

    return found


def bundle_windows_python(dest_pyvendor: Path) -> None:
    runtime_root = dest_pyvendor / "python"
    bin_src = Path(sys.executable).resolve().parent
    exe_src = Path(sys.executable).resolve()
    stdlib_src = Path(sysconfig.get_path("stdlib"))

    copy_file(exe_src, runtime_root / "python.exe")
    if exe_src.name.lower() != "python3.exe":
        copy_file(exe_src, runtime_root / "python3.exe")
    else:
        copy_file(exe_src, runtime_root / "python3.exe")

    for dll in find_windows_dlls(bin_src):
        copy_file(dll, runtime_root / dll.name)

    for helper in ("pythonw.exe",):
        candidate = bin_src / helper
        if candidate.exists():
            copy_file(candidate, runtime_root / helper)

    dlls_dir = Path(sys.base_prefix) / "DLLs"
    if dlls_dir.exists():
        copy_tree(dlls_dir, runtime_root / "DLLs")

    copy_tree(stdlib_src, runtime_root / "Lib")


def main() -> int:
    parser = argparse.ArgumentParser(description="Bundle vendored boxes.py and a local Python runtime")
    parser.add_argument("--dest", required=True, help="destination pyvendor directory")
    args = parser.parse_args()

    dest = Path(args.dest).resolve()
    copy_tree(PYVENDOR_SRC, dest)

    if os.name == "nt" or Path(sys.executable).suffix.lower() == ".exe":
        bundle_windows_python(dest)
    else:
        bundle_posix_python(dest)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
