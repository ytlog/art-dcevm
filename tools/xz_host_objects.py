"""Compile the vendored decoder as C for C++ host tests."""
import os
from pathlib import Path
import subprocess


def compile_xz(root: Path, out: Path):
    source = root / 'artdcevm/src/main/cpp/third_party/xz_embedded'
    objects = []
    for name in ('xz_dec_stream', 'xz_dec_lzma2', 'xz_crc32', 'xz_crc64'):
        obj = out / (name + '.o')
        subprocess.run([os.environ.get('CC', 'clang'), '-std=c99', '-O1', '-g',
                        '-DXZ_USE_CRC64', '-fsanitize=address,undefined',
                        '-fno-omit-frame-pointer', '-c', str(source / (name + '.c')),
                        '-o', str(obj)], check=True)
        objects.append(str(obj))
    return objects
