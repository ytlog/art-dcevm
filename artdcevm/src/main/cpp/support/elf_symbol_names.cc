#include "support/elf_symbol_names.h"
#include <cstring>
#include <cstdint>
#include <fstream>
#include <unordered_set>
#include <vector>

namespace artdcevm {
namespace {
// ELF64 file metadata, independent of the host's object-file format. These
// declarations describe section/name tables, never ART object layouts.
struct Header {
  unsigned char ident[16];
  uint16_t type, machine;
  uint32_t version;
  uint64_t entry, phoff, shoff;
  uint32_t flags;
  uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
};
struct Section {
  uint32_t name, type;
  uint64_t flags, addr, offset, size;
  uint32_t link, info;
  uint64_t addralign, entsize;
};
struct Symbol {
  uint32_t name;
  unsigned char info, other;
  uint16_t shndx;
  uint64_t value, size;
};
static_assert(sizeof(Header) == 64 && sizeof(Section) == 64 && sizeof(Symbol) == 24);
template <class T> bool Read(const std::vector<char>& bytes, size_t offset, T* result) {
  if (offset > bytes.size() || sizeof(T) > bytes.size() - offset) return false;
  std::memcpy(result, bytes.data() + offset, sizeof(T));
  return true;
}
bool Range(const std::vector<char>& bytes, uint64_t offset, uint64_t size) {
  return offset <= bytes.size() && size <= bytes.size() - offset;
}
}
std::unordered_map<std::string, std::string> ReadUniqueElfSymbolNames(const std::string& path) {
  std::unordered_map<std::string, std::string> names;
  std::unordered_set<std::string> ambiguous;
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) return names;
  const auto length = input.tellg();
  if (length < static_cast<std::streamoff>(sizeof(Header)) || length > 256 * 1024 * 1024) return names;
  std::vector<char> bytes(static_cast<size_t>(length));
  input.seekg(0);
  if (!input.read(bytes.data(), bytes.size())) return names;
  Header header{};
  const uint16_t endian = 1;
  if (*reinterpret_cast<const unsigned char*>(&endian) != 1) return names;
  if (!Read(bytes, 0, &header) || std::memcmp(header.ident, "\177ELF", 4) ||
      header.ident[4] != 2 || header.ident[5] != 1 || header.ident[6] != 1 || header.version != 1 ||
      header.machine != 183 || header.type != 3 ||
      header.shentsize != sizeof(Section) || header.shnum == 0 ||
      !Range(bytes, header.shoff, uint64_t{header.shnum} * sizeof(Section))) return names;
  for (size_t i = 0; i < header.shnum; ++i) {
    Section symbols{};
    if (!Read(bytes, header.shoff + i * sizeof(symbols), &symbols)) return {};
    if (symbols.type != 2) continue;  // SHT_SYMTAB
    if (symbols.entsize != sizeof(Symbol) || symbols.link >= header.shnum ||
        symbols.size % sizeof(Symbol) || !Range(bytes, symbols.offset, symbols.size)) return {};
    Section strings{};
    if (!Read(bytes, header.shoff + symbols.link * sizeof(strings), &strings) ||
        strings.type != 3 || !Range(bytes, strings.offset, strings.size)) return {};  // SHT_STRTAB
    for (size_t j = 0; j < symbols.size / sizeof(Symbol); ++j) {
      Symbol symbol{};
      if (!Read(bytes, symbols.offset + j * sizeof(symbol), &symbol)) return {};
      if (symbol.shndx == 0 || symbol.shndx >= header.shnum ||
          (symbol.info & 15) != 2 || (symbol.info >> 4) != 0) continue;
      if (symbol.name >= strings.size) return {};
      const char* start = bytes.data() + strings.offset + symbol.name;
      const size_t remaining = strings.size - symbol.name;
      const char* end = static_cast<const char*>(std::memchr(start, 0, remaining));
      if (!end) return {};
      std::string full(start, end);
      const auto suffix = full.rfind(".__uniq.");
      if (suffix == std::string::npos || suffix == 0 || suffix + 8 == full.size()) continue;
      bool numeric = true;
      for (size_t k = suffix + 8; k < full.size(); ++k) numeric &= full[k] >= '0' && full[k] <= '9';
      if (!numeric) continue;
      const auto base = full.substr(0, suffix);
      auto found = names.find(base);
      if (found != names.end() && found->second != full) ambiguous.insert(base);
      else names.emplace(base, std::move(full));
    }
  }
  for (const auto& name : ambiguous) names.erase(name);
  return names;
}
}
