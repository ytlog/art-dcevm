#include <cstdlib>
#include <cstring>
#include <iostream>
static unsigned forwarded_publication_starts=0,forwarded_publication_finishes=0;
static bool raw_publication_gc=false;
static int publication_collector=11;
static const char* publication_case="valid";
static void ExecuteInnerPublicationGC();
#include "gc_publication_success_body.inc"
static void ExecuteInnerPublicationGC(){
 // Ordinary calibration always observes a complete original pair. Only the
 // later exact RunState publication exercises the selected malformed source.
 const bool continuous=artdcevm::native_schema_run_state::RunState::Current()!=nullptr;
 const char* mode=continuous?publication_case:"valid";
 auto start=[&](int cause=12,int collector=0,void*heap_receiver=&owner_heap,void*self=thread.data()){
  owner_start_proxy(heap_receiver,self,cause,collector?collector:publication_collector);
 };
 auto finish=[&](int type=0){owner_finish_proxy(&owner_heap,thread.data(),type);};
 if(!std::strcmp(mode,"no-pair"))return;
 if(!std::strcmp(mode,"finish-first")){finish();return;}
 if(!std::strcmp(mode,"wrong-cause")){start(13);return;}
 if(!std::strcmp(mode,"wrong-collector")){start(12,publication_collector==11?13:11);return;}
 if(!std::strcmp(mode,"wrong-heap")){start(12,0,nullptr);return;}
 if(!std::strcmp(mode,"wrong-self")){start(12,0,&owner_heap,thread.data()+8);return;}
 start();
 if(!std::strcmp(mode,"missing-finish"))return;
 if(!std::strcmp(mode,"double-start")){start();return;}
 if(!std::strcmp(mode,"unwind"))throw 19;
 if(!std::strcmp(mode,"wrong-finish")){finish(1);return;}
 finish();
 if(!std::strcmp(mode,"double-finish"))finish();
}
int main(int argc,char**argv){
 if(argc>1)publication_case=argv[1];
 if(argc>2)publication_collector=std::atoi(argv[2]);
 assert(publication_collector==11||publication_collector==13);
 RunSuccessfulTransaction();
 // Ordinary profile executes the actual Start/Finish originals once. The
 // continuous call must borrow its stronger reservation and never call them.
 assert(forwarded_publication_starts==1&&forwarded_publication_finishes==1&&!raw_publication_gc);
 std::cout<<"Actual complete RunState one-pair publication: collector="<<publication_collector<<", originals=1 calibration / 0 continuation, final commit+cleanup+End PASS\n";
}
