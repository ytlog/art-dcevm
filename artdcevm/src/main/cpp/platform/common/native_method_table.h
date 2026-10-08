#pragma once
#include "platform/common/modern_field_metadata.h"
#include "platform/common/owned_method_table.h"
namespace artdcevm::native_method_table {
struct View {
  method_array::View methods;
  evolution::MappedDexMetadata dex;
  evolution::ClassSchema declarations;
};
// Native-only: called inside the original ART rooted/disabled-GC transaction.
// A complete independent Runnable oracle must already have selected Method11/12
// and the split/merged Class field source. DEX class_data independently supplies
// every declared index/count; the method array never defines its own oracle.
// Standard app DEX only; unsupported CompactDex fails without touching output.
bool Read(const modern_field_metadata::FieldMetadata&,
          const method_identity::Calibration&,void* rooted_class,
          View*,std::string* error);
}
