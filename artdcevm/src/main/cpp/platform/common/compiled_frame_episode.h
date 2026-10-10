#pragma once
#include <string>
namespace artdcevm::compiled_frame_fixture {
// Start/Close are ordinary Native JNI operations. Inspect alone enters the
// actual Runnable owner, registers source roots, stops/resumes and destroys
// the same-thread Pool before returning. Java lifecycle never nests its bridge.
template<class Start,class Inspect,class Close>
bool RunEpisode(Start start,Inspect inspect,Close close,std::string* error){
 const bool started=start(error);
 bool inspected=false;
 if(started){
  try{inspected=inspect(error);}
  catch(...){if(error)*error="Compiled fixture Runnable inspection threw";}
 }
 // A failed start may already have armed the worker. Close owns that cleanup
 // and preserves the original pending Throwable through release/join.
 std::string close_error;const bool closed=close(&close_error);
 if(!closed&&error&&((started&&inspected)||error->empty()))*error=close_error;
 return started&&inspected&&closed;
}
}
