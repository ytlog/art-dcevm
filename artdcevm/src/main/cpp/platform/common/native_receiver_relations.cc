#include "platform/common/native_receiver_relations.h"
#include <cstring>
namespace artdcevm::receiver_guard {
bool Relations::Next(void*klass,bool history,void**next)const noexcept{
 if(!next)return false;if(!klass){*next=nullptr;return true;}
 ClassView view;if(!access_.read||!access_.read(access_.owner,klass,&view)||!view.descriptor||(view.descriptor[0]!='L'&&view.descriptor[0]!='['))return false;
 if(history)return access_.obsolete&&access_.obsolete(access_.owner,klass,next);
 *next=view.parent;return true;
}
bool Relations::Acyclic(void*start,bool history)const noexcept{
 void*slow=start;void*fast=start;
 while(fast){if(!Next(slow,history,&slow)||!Next(fast,history,&fast)||!Next(fast,history,&fast))return false;if(fast&&fast==slow)return false;}return true;
}
bool Relations::Historical(void*current,void*declaring,bool*out)const noexcept{
 *out=false;if(!Acyclic(current,true))return false;
 ClassView owner;if(!access_.read(access_.owner,current,&owner))return false;
 for(void*old=current;old;){ClassView historical;if(!access_.read(access_.owner,old,&historical)||historical.loader!=owner.loader||!historical.descriptor||std::strcmp(historical.descriptor,owner.descriptor))return false;
  if(old==declaring)*out=true;if(!Next(old,true,&old))return false;
 }return true;
}
bool Relations::Normalize(void*declaring,void*receiver,void**current)const noexcept{
 if(!declaring||!receiver||!current||!access_.read||!Acyclic(receiver,false))return false;
 void*found=nullptr;for(void*klass=receiver;klass;){bool historical=false;if(!Historical(klass,declaring,&historical))return false;if(historical){if(found)return false;found=klass;}if(!Next(klass,false,&klass))return false;}
 *current=found?found:declaring;return true;
}
bool Relations::Assignable(void*declaring,void*receiver,bool*out)const noexcept{
 if(!out||!declaring||!receiver||!access_.read||!access_.interface_at||!Acyclic(receiver,false))return false;
 *out=false;ClassView target;if(!access_.read(access_.owner,declaring,&target))return false;
 for(void*klass=receiver;klass;){if(klass==declaring)*out=true;ClassView view;if(!access_.read(access_.owner,klass,&view))return false;
  if(target.flags&0x200u)for(size_t i=0;i<view.interface_count;++i){void*iface=nullptr;ClassView identity;if(!access_.interface_at(access_.owner,klass,i,&iface)||!iface||!access_.read(access_.owner,iface,&identity)||!(identity.flags&0x200u))return false;if(iface==declaring)*out=true;}
  if(!Next(klass,false,&klass))return false;
 }return true;
}
bool Relations::Special(void*declaring,void*caller,bool*out)const noexcept{
 if(!out||!declaring||!caller||!access_.read||!access_.interface_at)return false;
 ClassView target,source;if(!access_.read(access_.owner,declaring,&target)||!access_.read(access_.owner,caller,&source))return false;
 if(declaring==caller){*out=true;return true;}
 if(!(target.flags&0x200u)){if(!source.parent){*out=false;return true;}return Assignable(declaring,source.parent,out);}
 // JVMS special interface invocation requires a direct superinterface.
 // Complete IfTable membership would incorrectly authorize inherited edges.
 if(!access_.direct_at)return false;
 *out=false;for(size_t i=0;i<source.direct_count;++i){void*iface=nullptr;ClassView identity;
  if(!access_.direct_at(access_.owner,caller,i,&iface)||!iface||!access_.read(access_.owner,iface,&identity)||!(identity.flags&0x200u))return false;
  bool related=false;if(!Assignable(declaring,iface,&related))return false;if(related)*out=true;
 }return true;
}
}
