#pragma once
#include "platform/common/stack_visitor_service.h"

namespace stack_fixture {
struct Observation {
  unsigned constructs=0,walks=0,methods=0;
  void* thread=nullptr;
  void* context=nullptr;
  artdcevm::stack_visitor_service::WalkKind kind{};
  bool check=false,transitions=false;
};
extern Observation observed;
void* Symbol(artdcevm::stack_visitor_service::Declaration declaration,const char* name);
}
