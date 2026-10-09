#pragma once
#include "platform/common/native_hierarchy_graph.h"
namespace artdcevm::native_hierarchy_graph {
// Fixed ClassLinker::LoadSuperAndInterfaces/LinkSuperClass: interfaces have
// bootstrap Object as the native superclass, although public getSuperclass is
// null. Keep the public graph edge null; validate its separate native identity.
inline bool NativeSuperclass(const Graph&g,const Node&n,bool proposed,void**out)noexcept{
 if(!out)return false;
 if(n.flags&0x200u){
  if(n.parent!=SIZE_MAX||n.proposed_parent!=SIZE_MAX)return false;
  void*found=nullptr;
  for(const auto&object:g.nodes)if(!object.loader&&object.descriptor=="Ljava/lang/Object;"){
   if(found||!object.RootedClass()||(object.flags&0x200u))return false;
   found=proposed&&object.replacement?object.replacement:object.klass;
   if(!found)return false;
  }
  if(!found)return false;*out=found;return true;
 }
 size_t parent=proposed?n.proposed_parent:n.parent;
 if(parent==SIZE_MAX){*out=nullptr;return true;}
 if(parent>=g.nodes.size())return false;
 const auto&p=g.nodes[parent];*out=proposed&&p.replacement?p.replacement:p.klass;return *out!=nullptr;
}
}
