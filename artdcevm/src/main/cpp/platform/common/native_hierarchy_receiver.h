#pragma once
#include "platform/common/native_hierarchy_graph.h"
namespace artdcevm::modern_field_metadata {class FieldMetadata;}
namespace artdcevm::native_hierarchy_receiver {
// This receiver capability needs no dispatch interception: every preexisting
// class/interface assignability relation must survive the actual replacement
// graph. Narrowing still requires a separately implemented receiver-check owner
// covering interpreter/compiled/JNI/reflection/MH/VH; this class never grants it.
class Preservation final:public native_hierarchy_graph::ReceiverOwner {
 public:
 explicit Preservation(const modern_field_metadata::FieldMetadata&);
 ~Preservation();
 Preservation(const Preservation&)=delete;Preservation& operator=(const Preservation&)=delete;
 // Call between whole source operations, after graph authority cleanup. This
 // owner retains indices and declaration bytes, no independently managed roots.
 void Reset()noexcept;
 bool VerifyStaged(const native_hierarchy_graph::Graph&,
                   const std::vector<native_hierarchy_graph::Input>&,std::string*)const override;
 bool VerifyPrepared(const native_hierarchy_graph::Graph&,
                     const native_hierarchy_graph::Prepared&,std::string*)const override;
 bool Verify(const native_hierarchy_graph::Graph&,
             const source_commit::Plan&)const noexcept override;
 void BeginCommit(const native_hierarchy_graph::Graph&,
                  const source_commit::Plan&)const noexcept override;
 private:
 struct State;std::unique_ptr<State> state_;
};
}
