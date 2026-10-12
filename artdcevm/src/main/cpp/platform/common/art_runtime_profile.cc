#include "platform/common/art_runtime_profile.h"

#include <array>
#include <cstring>
#include <elf.h>
#include <link.h>
#include <string_view>

namespace artdcevm::method_retirement {
namespace {
// The audited device-01 ART libraries, recorded with their full SHA-256 in
// docs/VERIFICATION.md. Other API 36/Mainline/OEM builds keep Phase 2 only until
// their source declarations and runtime behavior have been independently checked.
constexpr std::array<unsigned char, 16> kArt = {
    0xea, 0x55, 0xd4, 0xac, 0x3d, 0xad, 0x73, 0x0a,
    0xfc, 0xc0, 0xea, 0x0e, 0x88, 0x5a, 0xaa, 0x76};
constexpr std::array<unsigned char, 16> kJvmti = {
    0x63, 0x42, 0x5f, 0xc0, 0x28, 0x7c, 0x8d, 0x7a,
    0x95, 0x16, 0x3b, 0xa0, 0x8a, 0xa8, 0xaf, 0x85};

struct Match { bool art = false; bool jvmti = false; };
size_t Padded(uint32_t size) { return (static_cast<size_t>(size) + 3) & ~size_t{3}; }

bool BuildIdMatches(const dl_phdr_info& library,
                    const std::array<unsigned char, 16>& expected) {
  for (ElfW(Half) i = 0; i < library.dlpi_phnum; ++i) {
    const auto& note = library.dlpi_phdr[i];
    if (note.p_type != PT_NOTE || note.p_memsz > 64 * 1024) continue;
    // Only inspect loader-mapped readable metadata. No ELF symbol value or ART
    // object is read; p_vaddr locates a public ELF note, not a private service.
    bool readable = false;
    for (ElfW(Half) j = 0; j < library.dlpi_phnum; ++j) {
      const auto& load = library.dlpi_phdr[j];
      if (load.p_type == PT_LOAD && (load.p_flags & PF_R) &&
          note.p_vaddr >= load.p_vaddr && note.p_vaddr - load.p_vaddr <= load.p_memsz &&
          note.p_memsz <= load.p_memsz - (note.p_vaddr - load.p_vaddr)) {
        readable = true;
        break;
      }
    }
    if (!readable) continue;
    auto* data = reinterpret_cast<const unsigned char*>(library.dlpi_addr + note.p_vaddr);
    size_t remaining = note.p_memsz;
    while (remaining >= sizeof(ElfW(Nhdr))) {
      ElfW(Nhdr) header;
      std::memcpy(&header, data, sizeof(header));
      data += sizeof(header);
      remaining -= sizeof(header);
      const size_t name = Padded(header.n_namesz);
      const size_t description = Padded(header.n_descsz);
      if (name > remaining || description > remaining - name) break;
      if (header.n_type == NT_GNU_BUILD_ID && header.n_namesz == 4 &&
          std::memcmp(data, "GNU", 4) == 0 && header.n_descsz == expected.size() &&
          std::memcmp(data + name, expected.data(), expected.size()) == 0) return true;
      data += name + description;
      remaining -= name + description;
    }
  }
  return false;
}
int Inspect(dl_phdr_info* library, size_t, void* state) {
  auto& match = *static_cast<Match*>(state);
  const std::string_view path(library->dlpi_name ? library->dlpi_name : "");
  const size_t slash = path.rfind('/');
  const auto filename = path.substr(slash == std::string_view::npos ? 0 : slash + 1);
  if (filename == "libart.so") match.art = BuildIdMatches(*library, kArt);
  if (filename == "libopenjdkjvmti.so") match.jvmti = BuildIdMatches(*library, kJvmti);
  return 0;
}
}

bool MatchesRuntimeProfile(std::string* reason) {
  Match match;
  dl_iterate_phdr(Inspect, &match);
  if (match.art && match.jvmti) return true;
  *reason = "method retirement disabled: ART build IDs are outside the verified Android 16 profile";
  return false;
}
}
