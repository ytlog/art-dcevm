#pragma once
#include <cstddef>
namespace artdcevm::source_commit {
// Shared exact lease key; pointee lifetime/root provenance is established by
// the native transaction owner. This comparison creates no independent roots.
template<class Plan> bool SameIdentity(const Plan& a,const Plan& b) {
 if(a.mirror!=b.mirror||a.new_classes_root!=b.new_classes_root||
    a.abstract_instances!=b.abstract_instances||a.old_classes!=b.old_classes||
    a.new_classes!=b.new_classes||a.old_instances!=b.old_instances||
    a.new_instances!=b.new_instances||a.class_root_origins.size()!=b.class_root_origins.size())return false;
 for(size_t i=0;i<a.class_root_origins.size();++i)
  if(a.class_root_origins[i].array!=b.class_root_origins[i].array||
     a.class_root_origins[i].index!=b.class_root_origins[i].index)return false;
 return true;
}
} // namespace artdcevm::source_commit
