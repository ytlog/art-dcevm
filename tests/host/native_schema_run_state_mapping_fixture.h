// Host mapping oracle: query actual readable regions while the aggregate owns
// its initial stop, then serve immutable bounds during final validation/commit.
struct RunStateMappings : ExplicitMappings {
 static bool Refresh(void* raw,std::string*) {
  auto& s=*static_cast<RunStateMappings*>(raw);
  auto* run=native_schema_run_state::RunState::Current();
  if(!run||!run->StoppedWindow()||!stopped||current!=thread.data())return false;
  s.ranges.clear();s.Cover(heap.base);s.Cover(thread.data());
  for(auto text:names)s.Cover(text);for(auto text:types)s.Cover(text);
  for(const auto& klass:public_classes)s.Cover(klass.descriptor.c_str());
  for(auto* file:{original_file,incoming_file}) {
   const auto* mapped=static_cast<const dex_mapping_abi::Prefix*>(file);
   s.Cover(mapped->begin);
  }
  return true;
 }
 static bool Read(void* raw,const void* address,size_t* extent) {
  auto* run=native_schema_run_state::RunState::Current();
  return run&&run->StoppedWindow()&&ExplicitMappings::Extent(raw,address,extent);
 }
};
