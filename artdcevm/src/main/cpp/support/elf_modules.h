#pragma once
#include <string>
namespace artdcevm {
struct LoadedModule { std::string path; std::string build_id; };
LoadedModule FindLoadedModule(const char* basename);
// Name resolution needs only the loader-owned path, never ELF notes/BuildID.
std::string FindLoadedModulePath(const char* basename);
}
