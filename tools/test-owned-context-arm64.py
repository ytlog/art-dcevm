#!/usr/bin/env python3
"""Execute actual ARM64 Context-consumer machine code in Unicorn.

Run in an isolated ignored venv with unicorn==2.1.4 and pyelftools==0.32.
This proves assembly restoration semantics with a simulated platform copy;
it does not calibrate ART private ABI on any device.
"""
import argparse
from pathlib import Path
import struct
import subprocess
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE
from unicorn import arm64_const as regs
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--ndk-bin', type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
cpp = root / 'artdcevm/src/main/cpp'
out = root / 'build/evidence/host/owned-context-consumer'
out.mkdir(parents=True, exist_ok=True)
obj = out / 'long-jump.o'
subprocess.run([str(args.ndk_bin / 'clang'), '--target=aarch64-linux-android30', '-c',
                str(cpp / 'platform/abi/arm64/owned_context_long_jump.S'), '-o', str(obj)], check=True)
subprocess.run([str(args.ndk_bin / 'clang++'), '--target=aarch64-linux-android30', '-std=c++17',
                '-Wall', '-Wextra', '-Werror', '-I', str(cpp), '-c',
                str(cpp / 'platform/common/owned_context_consumer.cc'),
                '-o', str(out / 'consumer-arm64.o')], check=True)
with obj.open('rb') as file:
    elf = ELFFile(file)
    text = elf.get_section_by_name('.text').data()
    symbols = {s.name: s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
    relocations = [r['r_offset'] for r in elf.get_section_by_name('.rela.text').iter_relocations()]
assert len(relocations) == 2  # One exact platform-copy call in each variant.
base, thread, landing = 0x100000, 0x300000, 0x400000
for reserved in (False, True):
    for marking_offset, suspend_offset in ((64, 512), (65532, 65520), (0, 8)):
        machine = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
        machine.mem_map(base, 0x1000)
        machine.mem_write(base, text)
        machine.mem_map(thread, 0x10000)
        machine.mem_map(landing, 0x1000)
        machine.mem_map(0x700000, 0x20000)
        machine.reg_write(regs.UC_ARM64_REG_SP, 0x71f000)
        original18 = 0x123456789abcdef0
        machine.reg_write(regs.UC_ARM64_REG_X18, original18)
        marker, trigger = 0x87654321, 0x1122334455667788
        machine.mem_write(thread + marking_offset, struct.pack('<I', marker))
        machine.mem_write(thread + suspend_offset, struct.pack('<Q', trigger))
        payload = marking_offset | suspend_offset << 16
        machine.reg_write(regs.UC_ARM64_REG_X0, 0xdeadbeef)  # Borrowed native Context placeholder.
        machine.reg_write(regs.UC_ARM64_REG_X1, payload)
        gprs = [0xa000000000000000 + i for i in range(34)]
        fprs = [0xb000000000000000 + i for i in range(32)]
        gprs[19], gprs[31], gprs[33] = thread, 0x718000, landing
        calls = []
        reached = []
        def hook(uc, address, size, data):
            if address in [base + offset for offset in relocations]:
                assert uc.reg_read(regs.UC_ARM64_REG_X0) == 0xdeadbeef
                gbuf, fbuf = uc.reg_read(regs.UC_ARM64_REG_X1), uc.reg_read(regs.UC_ARM64_REG_X2)
                assert gbuf % 16 == 0 and fbuf == gbuf + 272
                assert uc.reg_read(regs.UC_ARM64_REG_X3) == payload
                copied = list(gprs)
                copied[32] = payload  # Independent native-copy publication protocol.
                uc.mem_write(gbuf, struct.pack('<34Q', *copied))
                uc.mem_write(fbuf, struct.pack('<32Q', *fprs))
                calls.append((gbuf, fbuf))
                # Model arbitrary caller-saved clobbering by the native C++ copy.
                for i in range(18):
                    uc.reg_write(getattr(regs, 'UC_ARM64_REG_X' + str(i)), 0xc000 + i)
                uc.reg_write(regs.UC_ARM64_REG_X30, address + 4)
                uc.reg_write(regs.UC_ARM64_REG_PC, address + 4)
            elif address == landing:
                reached.append(address)
                uc.emu_stop()
        machine.hook_add(UC_HOOK_CODE, hook)
        name = 'ArtDcevmArm64LongJump' + ('Reserved' if reserved else 'Unreserved')
        machine.emu_start(base + symbols[name], landing + 4, count=1000)
        assert len(calls) == 1 and reached == [landing]
        for i in list(range(16)) + [19] + list(range(22, 31)):
            assert machine.reg_read(getattr(regs, 'UC_ARM64_REG_X' + str(i))) == gprs[i], (name, i)
        assert machine.reg_read(regs.UC_ARM64_REG_X18) == original18
        assert machine.reg_read(regs.UC_ARM64_REG_X20) == (marker if reserved else gprs[20])
        assert machine.reg_read(regs.UC_ARM64_REG_X21) == trigger
        assert machine.reg_read(regs.UC_ARM64_REG_SP) == gprs[31]
        assert machine.reg_read(regs.UC_ARM64_REG_PC) == gprs[33]
        for i in range(32):
            assert machine.reg_read(getattr(regs, 'UC_ARM64_REG_D' + str(i))) == fprs[i], (name, i)
result = 'ARM64 machine-code consumer: 6 cases passed; 32 D registers, all preserved X registers, X18, SP/PC and post-SP Thread refresh.\n'
(out / 'arm64-machine-result.txt').write_text(result)
print(result.strip())
