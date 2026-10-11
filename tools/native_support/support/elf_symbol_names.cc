#include "support/elf_symbol_names.h"
#include <cstring>
#include <cstdint>
#include <fstream>
#include <mutex>
extern "C" {
#include "xz.h"
}
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
struct SectionRange {uint64_t offset,size;};
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
namespace {
using Names = std::unordered_map<std::string, std::string>;
template<class Header,class Section,class Symbol>
bool Collect(const std::vector<char>& bytes, unsigned elf_class,unsigned machine,bool mini, Names& names,
             std::unordered_set<std::string>& ambiguous) {
  Header header{};
  const uint16_t endian = 1;
  if (*reinterpret_cast<const unsigned char*>(&endian) != 1) return false;
  if (!Read(bytes, 0, &header) || std::memcmp(header.ident, "\177ELF", 4) ||
      header.ident[4] != elf_class || header.ident[5] != 1 || header.ident[6] != 1 || header.version != 1 ||
      header.machine != machine || header.type != 3 ||
      header.ehsize != sizeof(Header) || header.shentsize != sizeof(Section) || header.shnum == 0 ||
      !Range(bytes, header.shoff, uint64_t{header.shnum} * sizeof(Section))) return false;
  for (size_t i = 0; i < header.shnum; ++i) {
    Section symbols{};
    if (!Read(bytes, header.shoff + i * sizeof(symbols), &symbols)) return false;
    if (symbols.type != 2) continue;  // SHT_SYMTAB
    if (symbols.entsize != sizeof(Symbol) || symbols.link >= header.shnum ||
        symbols.size % sizeof(Symbol) || !Range(bytes, symbols.offset, symbols.size)) return false;
    Section strings{};
    if (!Read(bytes, header.shoff + symbols.link * sizeof(strings), &strings) ||
        strings.type != 3 || !Range(bytes, strings.offset, strings.size)) return false;  // SHT_STRTAB
    for (size_t j = 0; j < symbols.size / sizeof(Symbol); ++j) {
      Symbol symbol{};
      if (!Read(bytes, symbols.offset + j * sizeof(symbol), &symbol)) return false;
      if (symbol.shndx == 0 || symbol.shndx >= header.shnum ||
          (symbol.info & 15) != 2 || ((symbol.info >> 4) != 0 && !mini)) continue;
      if (symbol.name >= strings.size) return false;
      const char* start = bytes.data() + strings.offset + symbol.name;
      const size_t remaining = strings.size - symbol.name;
      const char* end = static_cast<const char*>(std::memchr(start, 0, remaining));
      if (!end) return false;
      std::string full(start, end);
      const auto suffix = full.find(".__uniq.");
      if (suffix == std::string::npos || suffix == 0 || suffix + 8 == full.size()) continue;
      // Clang LTO can append a second numeric identity to the unique
      // internal-linkage name. Accept only the observed strict grammar:
      // .__uniq.<digits> optionally followed by .llvm.<digits>.
      const auto llvm = full.find(".llvm.", suffix + 8);
      auto decimal = [&](size_t first, size_t last) {
        if (first == last) return false;
        for (size_t k = first; k < last; ++k)
          if (full[k] < '0' || full[k] > '9') return false;
        return true;
      };
      if (!decimal(suffix + 8, llvm == std::string::npos ? full.size() : llvm) ||
          (llvm != std::string::npos && !decimal(llvm + 6, full.size()))) continue;
      const auto base = full.substr(0, suffix);
      auto found = names.find(base);
      if (found != names.end() && found->second != full) ambiguous.insert(base);
      else names.emplace(base, std::move(full));
    }
  }
  return true;
}
bool Decode(const std::vector<char>& bytes, const SectionRange& section, std::vector<char>* output) {
  constexpr size_t kLimit = 32 * 1024 * 1024;
  if (section.size > kLimit || !Range(bytes, section.offset, section.size)) return false;
  static std::once_flag crc;
  std::call_once(crc, [] { xz_crc32_init(); xz_crc64_init(); });
  xz_dec* decoder = xz_dec_init(XZ_SINGLE, 0);
  if (!decoder) return false;
  bool decoded = false;
  for (size_t capacity = 256 * 1024; capacity <= kLimit; capacity *= 2) {
    output->resize(capacity);
    xz_buf buffer{};
    buffer.in = reinterpret_cast<const uint8_t*>(bytes.data() + section.offset);
    buffer.in_size = section.size;
    buffer.out = reinterpret_cast<uint8_t*>(output->data());
    buffer.out_size = capacity;
    const auto result = xz_dec_run(decoder, &buffer);
    if (result == XZ_STREAM_END && buffer.in_pos == buffer.in_size) {
      output->resize(buffer.out_pos); decoded = true; break;
    }
    if (result != XZ_BUF_ERROR) break;
  }
  xz_dec_end(decoder);
  if (!decoded) output->clear();
  return decoded;
}
template<class Header,class Section>
bool MiniSection(const std::vector<char>& bytes, SectionRange* output, bool* present) {
  Header h{};
  if (!Read(bytes, 0, &h)) return false;
  *present = false;
  if (!h.shstrndx) return true;
  if (h.shstrndx >= h.shnum) return false;
  Section strings{};
  if (!Read(bytes, h.shoff + h.shstrndx * sizeof(Section), &strings) ||
      strings.type != 3 || !Range(bytes, strings.offset, strings.size)) return false;
  for (size_t i = 0; i < h.shnum; ++i) {
    Section section{};
    if (!Read(bytes, h.shoff + i * sizeof(Section), &section) || section.name >= strings.size) return false;
    const char* name = bytes.data() + strings.offset + section.name;
    if (!std::memchr(name, 0, strings.size - section.name)) return false;
    if (std::strcmp(name, ".gnu_debugdata")) continue;
    if (*present || section.type != 1) return false;
    *present = true; *output = {section.offset,section.size};
  }
  return true;
}
template<class H,class S,class Y> bool CollectFile(const std::vector<char>& bytes,
    unsigned elf_class,unsigned machine,Names& names,std::unordered_set<std::string>& ambiguous) {
  if(!Collect<H,S,Y>(bytes,elf_class,machine,false,names,ambiguous))return false;
  SectionRange section{};bool present=false;
  if(!MiniSection<H,S>(bytes,&section,&present))return false;
  if(present) {
    std::vector<char> mini;
    if(!Decode(bytes,section,&mini) || !Collect<H,S,Y>(mini,elf_class,machine,true,names,ambiguous))return false;
  }
  return true;
}
}
std::unordered_map<std::string, std::string> ReadUniqueElfSymbolNames(const std::string& path) {
  Names names;
  std::unordered_set<std::string> ambiguous;
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) return names;
  const auto length = input.tellg();
  if (length < static_cast<std::streamoff>(sizeof(Header)) || length > 256 * 1024 * 1024) return names;
  std::vector<char> bytes(static_cast<size_t>(length));
  input.seekg(0);
  if(!input.read(bytes.data(),bytes.size()))return {};
  const bool collected=CollectFile<Header,Section,Symbol>(bytes,2,183,names,ambiguous);
  if(!collected)return {};
  for (const auto& name : ambiguous) names.erase(name);
  return names;
}
}
