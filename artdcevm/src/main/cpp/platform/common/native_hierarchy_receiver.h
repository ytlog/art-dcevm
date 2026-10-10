#pragma once
#include "platform/common/native_hierarchy_graph.h"
namespace artdcevm::modern_field_metadata {class FieldMetadata;struct ReadContext;}
namespace artdcevm::native_narrowing {class Authority;}
namespace artdcevm::native_hierarchy_receiver {
// Without an exact concrete narrowing owner, every preexisting class/interface
// relation must survive the replacement graph. An attached owner may stage a
// refundable narrowing only with actual permanent dispatch proof; final
// publication additionally requires exact retained-storage and frame recovery.
class Preservation final:public native_hierarchy_graph::ReceiverOwner {
 public:
 explicit Preservation(const modern_field_metadata::FieldMetadata&);
 ~Preservation();
 Preservation(const Preservation&)=delete;Preservation& operator=(const Preservation&)=delete;
 // Call between whole source operations, after graph authority cleanup. This
 // owner retains indices and declaration bytes, no independently managed roots.
 void Reset()noexcept;
 // Optional concrete per-operation owner; absence retains all old relations.
 bool UseNarrowing(native_narrowing::Authority&)noexcept;
 void UseReadContext(const modern_field_metadata::ReadContext&)noexcept;
 bool VerifyStaged(const native_hierarchy_graph::Graph&,
                   const std::vector<native_hierarchy_graph::Input>&,std::string*)const override;
 bool VerifyPrepared(const native_hierarchy_graph::Graph&,
                     const native_hierarchy_graph::Prepared&,std::string*)const override;
 bool VerifyAllocation(const native_hierarchy_graph::Graph&g,
                       const native_hierarchy_graph::Prepared&p,std::string*e)const override{return !p.new_class&&VerifyPrepared(g,p,e);}
 bool Verify(const native_hierarchy_graph::Graph&,
             const source_commit::Plan&)const noexcept override;
 bool VerifyFinal(const native_hierarchy_graph::Graph&,const source_commit::Plan&)const noexcept override;
 void BeginCommit(const native_hierarchy_graph::Graph&,
                  const source_commit::Plan&)const noexcept override;
 private:
 struct State;std::unique_ptr<State> state_;
};
}
