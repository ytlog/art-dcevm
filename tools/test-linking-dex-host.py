#!/usr/bin/env python3
"""Validate ART's private selection DEX independently of the runtime linker."""
import hashlib
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import zlib

from evidence_paths import new_evidence_directory

ROOT = Path(__file__).resolve().parents[1]
SOURCE = r'''
#include "evolution/linking_dex.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace artdcevm::evolution;
void require(bool value) { if (!value) throw std::runtime_error("private DEX invariant"); }
int main(int argc, char** argv) {
  require(argc == 2);
  for (int count : {0, 1, 2, 127, 128, 300}) {
    std::vector<std::string> parents;
    for (int n = count; n > 0; --n) parents.push_back("Lsample/Interface" + std::to_string(n) + ";");
    if (count) parents.push_back(parents.front());
    auto bytes = linking_dex::Class("Lsample/Selection;", parents);
    const auto unchanged = bytes;
    DexView dex{bytes.data(), bytes.size(), bytes.data(), bytes.size()};
    ClassSchema schema;
    std::string error;
    require(ReadClassDeclarations(dex, &schema, &error));
    require(schema.descriptor == "Lsample/Selection;" && schema.access == 0x401);
    require(schema.superclass == "Ljava/lang/Object;" && schema.methods.empty());
    require(schema.interfaces.size() == static_cast<size_t>(count));
    std::vector<uint16_t> indices;
    require(ReadClassInterfaceIndices(dex, 0, &indices, &error) && indices.size() == schema.interfaces.size());
    for (size_t n = 0; n < indices.size(); ++n) {
      std::string name;
      require(ReadTypeDescriptor(dex, indices[n], &name, &error) && name == schema.interfaces[n]);
    }
    auto skeleton = linking_dex::WithoutInterfaces(dex, 0);
    require(bytes == unchanged);
    ClassSchema masked;
    require(ReadClassDeclarations({skeleton.data(), skeleton.size(), skeleton.data(), skeleton.size()}, &masked, &error));
    require(masked.interfaces.empty() && masked.descriptor == schema.descriptor && masked.access == schema.access);
    const size_t offset = linking_dex::Read32(bytes.data() + 100) + 12;
    for (size_t n = 0; n < bytes.size(); ++n)
      if (!(n >= 8 && n < 32) && !(n >= offset && n < offset + 4)) require(bytes[n] == skeleton[n]);
    try { linking_dex::WithoutInterfaces(dex, 1); require(false); } catch (const std::invalid_argument&) {}
    std::ofstream(std::string(argv[1]) + "/" + std::to_string(count) + ".dex", std::ios::binary)
        .write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    std::ofstream(std::string(argv[1]) + "/" + std::to_string(count) + "-masked.dex", std::ios::binary)
        .write(reinterpret_cast<const char*>(skeleton.data()), skeleton.size());
  }
  std::cout << "Private linking DEX declarations and immutable masks PASS\n";
}
'''


def main():
    evidence = new_evidence_directory(ROOT / 'build/evidence/host/linking-dex', 'host')
    with tempfile.TemporaryDirectory() as name:
        work = Path(name)
        source = work / 'test.cc'
        source.write_text(SOURCE)
        native = ROOT / 'artdcevm/src/main/cpp'
        binary = work / 'test'
        commands = ([shutil.which('clang++') or shutil.which('c++'), '-std=c++17', '-Wall', '-Wextra',
                     '-Werror', '-I' + str(native), str(source), str(native / 'evolution/dex_schema.cc'), '-o', str(binary)],
                    [str(binary), str(work)])
        for index, command in enumerate(commands):
            result = subprocess.run(command, capture_output=True, text=True)
            (evidence / ('compile.log' if index == 0 else 'test.log')).write_text(result.stdout + result.stderr)
            if result.returncode:
                raise RuntimeError('Private linking DEX check failed; see ignored host/linking-dex evidence')
        files = list(work.glob('*.dex'))
        assert len(files) == 12
        for path in files:
            data = path.read_bytes()
            assert data[12:32] == hashlib.sha1(data[32:]).digest()
            assert struct.unpack_from('<I', data, 8)[0] == zlib.adler32(data[12:])
            assert struct.unpack_from('<I', data, 32)[0] == len(data)
            map_offset = struct.unpack_from('<I', data, 52)[0]
            count = struct.unpack_from('<I', data, map_offset)[0]
            items = [struct.unpack_from('<HHII', data, map_offset + 4 + n * 12) for n in range(count)]
            offsets = [item[3] for item in items]
            assert offsets == sorted(offsets)
            assert items[-1] == (0x1000, 0, 1, map_offset)
        print('Private linking DEX: 12 files, independent SHA-1/Adler32/map checks PASS')


if __name__ == '__main__':
    main()
