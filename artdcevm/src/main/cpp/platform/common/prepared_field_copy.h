#pragma once
#include "platform/common/field_copy.h"
#include <string>
#include <vector>
namespace artdcevm::field_copy {
// Readonly full-table preflight. Commit consumes native field identities only;
// it performs no metadata search, allocation, JNI or type reinterpretation.
struct ClassPair {void* before;void* after;};
class PreparedPlan {
 public:
  static bool Prepare(const FieldCopyAccess&,bool is_static,void* target_class,void* source_class,PreparedPlan*,std::string*,const std::vector<ClassPair>& pairs={},bool allow_removed_ancestors=false);
  bool Validate(const FieldCopyAccess&)const;
  void CopyAndClear(const FieldCopyAccess&,void* target,void* source)const noexcept;
  size_t RetainedCount()const{return copies_.size();}
  size_t RetiredCount()const{return clear_.size()-copies_.size();}
 private:
  struct Field {void* identity;std::string name,descriptor;};
  struct Class {void* identity;void* parent;void* loader;std::string descriptor;bool object;std::vector<Field> fields;};
  struct Copy {void* source;void* target;char type;};
  bool is_static_=false;
  std::vector<Class> source_,target_;
  std::vector<Copy> copies_,clear_;
};
}
