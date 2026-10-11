#!/usr/bin/env python3
"""Create genuine NDK-linked ARM64 ELF inputs; import prepare() in host runners."""
import os
import subprocess
from pathlib import Path

def prepare(out: Path) -> Path:
    out.mkdir(parents=True, exist_ok=True)
    sdk = Path(os.environ.get('ANDROID_SDK_ROOT', str(Path.home() / 'Library/Android/sdk')))
    clang = sorted(sdk.glob('ndk/*/toolchains/llvm/prebuilt/darwin-x86_64/bin/clang'))[-1]
    source = out / 'upcall-stubs.c'
    source.write_text('''
__attribute__((visibility("default"),noinline)) unsigned long art_quick_invoke_stub(unsigned long a) {
  volatile unsigned long v = a + 19; return v * 3;
}
__attribute__((visibility("default"),noinline)) unsigned long art_quick_invoke_static_stub(unsigned long a) {
  volatile unsigned long v = a + 29; return v * 5;
}
''')
    elf = out / 'libart.so'
    cmd = [str(clang), '--target=aarch64-linux-android30', '-fuse-ld=lld', '-nostdlib', '-shared',
           '-fPIC', '-O1', '-g', '-Wl,-soname,libart.so', '-Wl,--hash-style=both', str(source), '-o', str(elf)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    (out / 'elf-compile.log').write_text(r.stdout + r.stderr)
    r.check_returncode()
    return elf

if __name__ == '__main__':
    import sys
    print(prepare(Path(sys.argv[1])))
