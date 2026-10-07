#pragma once
#include <cstdint>
namespace artdcevm::android13 {
using BeginHierarchyState = uint32_t (*)();
using EndHierarchyState = void (*)(uint32_t);
using HierarchyStateQuery = bool (*)();
using ConfigureHierarchyState = void (*)(BeginHierarchyState,EndHierarchyState,
                                         HierarchyStateQuery,HierarchyStateQuery);
}
