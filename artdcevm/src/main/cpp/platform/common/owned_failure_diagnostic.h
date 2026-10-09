#pragma once
#include <jvmti.h>
#include <string>
#include <new>
#include <cstdio>
namespace artdcevm::owned_failure_diagnostic {
// Publishing occurs only at refundable preparation or after original source
// return. An optional diagnostic cannot change the existing JVMTI outcome.
inline void Publish(const char* text,std::string* error) noexcept {
 if(error&&text)try{*error=text;}catch(const std::bad_alloc&){}
}
inline int Unavailable(std::string* error,const char* boundary) noexcept {
 if(error&&error->empty()){
  char text[192];std::snprintf(text,sizeof(text),"Owned preparation rejected at %s before original source entry",boundary?boundary:"unknown preparation boundary");
  Publish(text,error);
 }
 return JVMTI_ERROR_NOT_AVAILABLE;
}
}
