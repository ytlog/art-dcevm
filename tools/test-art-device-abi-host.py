#!/usr/bin/env python3
"""Check collected ELF identity independently of device ABI advertisements."""
import importlib.util
from pathlib import Path
import struct
import unittest

spec = importlib.util.spec_from_file_location('collector', Path(__file__).with_name('collect-art-device.py'))
collector = importlib.util.module_from_spec(spec)
spec.loader.exec_module(collector)


def elf(width, machine):
    header = bytearray(64)
    header[:7] = b'\x7fELF' + bytes([width, 1, 1])
    struct.pack_into('<H', header, 18, machine)
    return header


class LibraryAbiTest(unittest.TestCase):
    def test_both_widths_and_mounted_apex_paths(self):
        for root in ['/system', '/apex/com.android.art', '/apex/com.android.art@123']:
            self.assertEqual(collector.library_abi(root + '/lib64/libart.so', elf(2, 183)), 'arm64-v8a')
            self.assertEqual(collector.library_abi(root + '/lib/libart.so', elf(1, 40)), 'armeabi-v7a')

    def test_reject_wrong_width_machine_directory_and_endianness(self):
        big_endian = elf(1, 40)
        big_endian[5] = 2
        for path, data in [
            ('/system/lib64/libart.so', elf(1, 40)),
            ('/system/lib/libart.so', elf(2, 183)),
            ('/system/lib64/libart.so', elf(2, 62)),
            ('/system/lib/libart.so', elf(1, 3)),
            ('/system/lib64/libart.so', elf(1, 183)),
            ('/system/lib/libart.so', elf(2, 40)),
            ('/system/other/libart.so', elf(2, 183)),
            ('/system/lib/libart.so', big_endian),
            ('/system/lib/libart.so', b'\x7fELF'),
            ('/system/lib64/libart.so', b'not ELF' * 10),
        ]:
            with self.subTest(path=path, header=data[:20]):
                with self.assertRaises(ValueError):
                    collector.library_abi(path, data)


if __name__ == '__main__':
    unittest.main()
