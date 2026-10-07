#pragma once
#include <string>
namespace artdcevm {
struct LoadedModule { std::string path; std::string build_id; };
LoadedModule FindLoadedModule(const char* basename);
}
