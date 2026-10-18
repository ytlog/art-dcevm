#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include "evolution/class_schema.h"

namespace artdcevm::evolution::linking_dex {
inline uint32_t Read32(const uint8_t* p) {
  return uint32_t{p[0]} | (uint32_t{p[1]} << 8) | (uint32_t{p[2]} << 16) | (uint32_t{p[3]} << 24);
}
inline void Put32(std::vector<uint8_t>& bytes, size_t at, uint32_t value) {
  for (size_t n = 0; n < 4; ++n) bytes.at(at + n) = static_cast<uint8_t>(value >> (8 * n));
}
inline void Append32(std::vector<uint8_t>& bytes, uint32_t value) {
  const size_t at = bytes.size(); bytes.resize(at + 4); Put32(bytes, at, value);
}
inline void Append16(std::vector<uint8_t>& bytes, uint16_t value) {
  bytes.push_back(static_cast<uint8_t>(value)); bytes.push_back(static_cast<uint8_t>(value >> 8));
}
inline void Align(std::vector<uint8_t>& bytes) { while (bytes.size() % 4) bytes.push_back(0); }
inline void Leb(std::vector<uint8_t>& bytes, size_t value) {
  do { const uint8_t byte = value & 0x7f; value >>= 7; bytes.push_back(byte | (value ? 0x80 : 0)); } while (value);
}
inline uint32_t Rotate(uint32_t value, unsigned n) { return (value << n) | (value >> (32 - n)); }
// DEX's content signature and checksum. Independent host tests compare them
// with hashlib/zlib; neither hash authenticates a reload request.
inline void Seal(std::vector<uint8_t>& bytes) {
  std::vector<uint8_t> message(bytes.begin() + 32, bytes.end());
  const uint64_t bits = uint64_t{message.size()} * 8;
  message.push_back(0x80);
  while (message.size() % 64 != 56) message.push_back(0);
  for (int n = 7; n >= 0; --n) message.push_back(static_cast<uint8_t>(bits >> (8 * n)));
  std::array<uint32_t, 5> hash{0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};
  for (size_t offset = 0; offset < message.size(); offset += 64) {
    uint32_t words[80]{};
    for (size_t i = 0; i < 16; ++i)
      for (size_t n = 0; n < 4; ++n) words[i] = (words[i] << 8) | message[offset + 4 * i + n];
    for (size_t i = 16; i < 80; ++i) words[i] = Rotate(words[i-3] ^ words[i-8] ^ words[i-14] ^ words[i-16], 1);
    uint32_t a=hash[0], b=hash[1], c=hash[2], d=hash[3], e=hash[4];
    for (size_t i = 0; i < 80; ++i) {
      const uint32_t f = i < 20 ? (b & c) | (~b & d) : i < 40 ? b ^ c ^ d : i < 60 ? (b & c) | (b & d) | (c & d) : b ^ c ^ d;
      const uint32_t k = i < 20 ? 0x5a827999 : i < 40 ? 0x6ed9eba1 : i < 60 ? 0x8f1bbcdc : 0xca62c1d6;
      const uint32_t next = Rotate(a, 5) + f + e + k + words[i];
      e=d; d=c; c=Rotate(b,30); b=a; a=next;
    }
    hash[0]+=a; hash[1]+=b; hash[2]+=c; hash[3]+=d; hash[4]+=e;
  }
  for (size_t i = 0; i < 5; ++i)
    for (size_t n = 0; n < 4; ++n) bytes[12 + 4*i + n] = static_cast<uint8_t>(hash[i] >> (24 - 8*n));
  uint32_t a=1, b=0;
  for (size_t i=12; i<bytes.size(); ++i) { a=(a+bytes[i])%65521; b=(b+a)%65521; }
  Put32(bytes, 8, (b << 16) | a);
}

// A private, empty abstract class asks ART to resolve defaults across this final
// interface graph. No helper Class is entered into a loader's class table.
inline std::vector<uint8_t> Class(const std::string& descriptor, std::vector<std::string> parents) {
  std::sort(parents.begin(), parents.end()); parents.erase(std::unique(parents.begin(), parents.end()), parents.end());
  std::vector<std::string> types = parents;
  types.push_back("Ljava/lang/Object;"); types.push_back(descriptor);
  std::sort(types.begin(), types.end()); types.erase(std::unique(types.begin(), types.end()), types.end());
  if (types.size() > UINT16_MAX) throw std::invalid_argument("too many selection interface types");
  const auto index = [&](const std::string& name) { return static_cast<uint32_t>(std::lower_bound(types.begin(),types.end(),name)-types.begin()); };
  const uint32_t strings=112, ids=strings+4*types.size(), defs=ids+4*types.size(), data=defs+32;
  std::vector<uint8_t> bytes(data,0);
  std::memcpy(bytes.data(), "dex\n035\0", 8);
  Put32(bytes,36,112); Put32(bytes,40,0x12345678);
  Put32(bytes,56,types.size()); Put32(bytes,60,strings); Put32(bytes,64,types.size()); Put32(bytes,68,ids);
  Put32(bytes,96,1); Put32(bytes,100,defs); Put32(bytes,108,data);
  Put32(bytes,defs,index(descriptor)); Put32(bytes,defs+4,0x0401); Put32(bytes,defs+8,index("Ljava/lang/Object;"));
  Put32(bytes,defs+16,UINT32_MAX);
  const uint32_t list=parents.empty()?0:bytes.size();
  if (list) { Append32(bytes,parents.size()); for (const auto& parent:parents) Append16(bytes,index(parent)); Align(bytes); }
  Put32(bytes,defs+12,list);
  const uint32_t string_data=bytes.size();
  for (size_t i=0; i<types.size(); ++i) {
    Put32(bytes,strings+4*i,bytes.size()); Put32(bytes,ids+4*i,i);
    size_t units=0; for (uint8_t c:types[i]) if ((c&0xc0)!=0x80) ++units;
    Leb(bytes,units); bytes.insert(bytes.end(),types[i].begin(),types[i].end()); bytes.push_back(0);
  }
  Align(bytes); const uint32_t map=bytes.size();
  Append32(bytes,list?7:6);
  const auto item=[&](uint16_t type,uint32_t count,uint32_t offset) { Append16(bytes,type); Append16(bytes,0); Append32(bytes,count); Append32(bytes,offset); };
  item(0,1,0); item(1,types.size(),strings); item(2,types.size(),ids); item(6,1,defs);
  if (list) item(0x1001,1,list);
  item(0x2002,types.size(),string_data); item(0x1000,1,map);
  Put32(bytes,32,bytes.size()); Put32(bytes,52,map); Put32(bytes,104,bytes.size()-data);
  Seal(bytes); return bytes;
}

// Keep every id and code item identical; only the private ClassDef loses its
// own interfaces while ART assigns the ordinary class-method vtable slots.
// Compact DEX keeps its separate data mapping; no live ART mapping is written.
inline std::vector<uint8_t> WithoutInterfaces(const DexView& dex, uint32_t index) {
  std::vector<std::string> interfaces;
  std::string error;
  if (!ReadClassInterfaces(dex, index, &interfaces, &error)) throw std::invalid_argument(error);
  std::vector<uint8_t> bytes(dex.begin, dex.begin + dex.size);
  const uint32_t definitions = Read32(bytes.data() + 100);
  Put32(bytes, size_t{definitions} + size_t{index} * 32 + 12, 0);
  // Standard checksums remain valid even though OpenCommon also supports
  // compact files with a separate data mapping (whose signature is retained).
  if (std::memcmp(bytes.data(), "dex\n", 4) == 0) Seal(bytes);
  return bytes;
}
}  // namespace artdcevm::evolution::linking_dex
