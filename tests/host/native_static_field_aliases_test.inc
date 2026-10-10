 std::unordered_map<void*,void*> mapping{{old_fields[0],new_fields[0]}};
 source_commit::Plan source{old_class,class_array,{old_class},{new_class},{},{},false};
 modern_field_metadata::ReadContext readable{&heap,HeapExtent};
 const auto os_before=method_identity::MappingOsQueries();
 native_static_field_aliases::Plan first;
 new_fields[0]->offset=132;new_fields[0]->dex_field_index=17;new_fields[0]->access_flags=8|0x40;
 assert(first.Prepare(hooks,metadata,source,&mapping,&error,&readable));
 assert(first.Entries().size()==1&&first.Validate());
 // Current definition changes offset/index/volatile flags. The old opaque
 // native ID stays unchanged and reads/writes the replacement payload.
 Put32(new_class,132,431);forbid_alias_allocation=true;assert(first.Validate());first.Rebind();forbid_alias_allocation=false;
 assert(old_fields[0]->declaring_class==uintptr_t(new_class)&&old_fields[0]->offset==132&&old_fields[0]->dex_field_index==17&&old_fields[0]->access_flags==(8|0x40));
 assert(Read32(Root(old_fields[0]),old_fields[0]->offset)==431);Put32(Root(old_fields[0]),old_fields[0]->offset,527);assert(Read32(new_class,132)==527);
 assert(!first.Validate()); // A consumed preflight is never reusable.
 auto* third=static_cast<class_metadata::Class17*>(heap.New(160));InitializeClass(third);
 std::array<abi::arm64::Field*,4> third_fields{};MakeFields(third,third_fields);
 alias_history[new_class]=old_class;
 source_commit::Plan second_source{new_class,class_array,{new_class},{third},{},{},false};
 mapping={{new_fields[0],third_fields[0]}};
 native_static_field_aliases::Plan second;
 third_fields[0]->offset=136;third_fields[0]->dex_field_index=23;
 assert(second.Prepare(hooks,metadata,second_source,&mapping,&error,&readable));
 assert(second.Entries().size()==2&&mapping.at(old_fields[0])==third_fields[0]&&second.Validate());
 Put32(third,136,829);
 second.Rebind();assert(Root(old_fields[0])==third&&Root(new_fields[0])==third);
 assert(Read32(Root(old_fields[0]),old_fields[0]->offset)==829);
 // Reintroduced same-name fields must not revive a previously removed ID.
 old_fields[0]->declaring_class=uintptr_t(old_class);
 native_static_field_aliases::Plan removed_prior;mapping={{new_fields[0],third_fields[0]}};
 // Restore the actual before-state for the next independent preflight.
 new_fields[0]->declaring_class=uintptr_t(new_class);
 assert(removed_prior.Prepare(hooks,metadata,second_source,&mapping,&error,&readable));
 assert(removed_prior.Entries().size()==1&&mapping.count(old_fields[0])==0);
 old_fields[0]->declaring_class=uintptr_t(new_class);*old_fields[0]=*new_fields[0];
 native_static_field_aliases::Plan deleted;mapping={{new_fields[0],nullptr}};
 assert(deleted.Prepare(hooks,metadata,second_source,&mapping,&error,&readable));
 assert(mapping.at(old_fields[0])==nullptr&&deleted.Entries()[1].current==new_fields[0]);
 deleted.Rebind();assert(Root(old_fields[0])==new_class); // Retirement retains genuine source metadata.
 // Prepared managed locations follow an independently moved root array.
 std::vector<void*> slots;
 prepared_roots::Registrar reg{&slots,[](void* p,void* value,size_t* out){auto& values=*static_cast<std::vector<void*>*>(p);*out=values.size();values.push_back(value);return true;}};
 assert(deleted.CollectManagedRoots(reg));
 auto* moved_current=static_cast<class_metadata::Class17*>(heap.New(160));std::memcpy(moved_current,new_class,160);
 auto* moved_history=static_cast<class_metadata::Class17*>(heap.New(160));std::memcpy(moved_history,old_class,160);
 for(auto& slot:slots){if(slot==new_class)slot=moved_current;else if(slot==old_class)slot=moved_history;}
 alias_history.erase(new_class);alias_history[moved_current]=moved_history;
 old_fields[0]->declaring_class=uintptr_t(moved_current);new_fields[0]->declaring_class=uintptr_t(moved_current);
 prepared_roots::Resolver resolve{&slots,[](void* p,size_t n)noexcept->void*{auto& values=*static_cast<std::vector<void*>*>(p);return n<values.size()?values[n]:nullptr;}};
 forbid_alias_allocation=true;assert(deleted.CanRefresh(resolve));deleted.Refresh(resolve);assert(deleted.Validate());forbid_alias_allocation=false;
 deleted.Rebind();assert(Root(old_fields[0])==moved_current);
 // Restore the separate ordinary fixture for remaining negative preflights.
 old_fields[0]->declaring_class=uintptr_t(new_class);new_fields[0]->declaring_class=uintptr_t(new_class);alias_history.erase(moved_current);alias_history[new_class]=old_class;
 for(size_t word:{size_t(4),size_t(8),size_t(12)}){uint32_t saved=Read32(old_fields[0],word);Put32(old_fields[0],word,saved^1);
  native_static_field_aliases::Plan bad;mapping={{new_fields[0],third_fields[0]}};assert(!bad.Prepare(hooks,metadata,second_source,&mapping,&error,&readable));Put32(old_fields[0],word,saved);}
 alias_history[old_class]=new_class;native_static_field_aliases::Plan cycle;mapping={{new_fields[0],third_fields[0]}};assert(!cycle.Prepare(hooks,metadata,second_source,&mapping,&error,&readable));alias_history.erase(old_class);
 assert(method_identity::MappingOsQueries()==os_before);
 const auto saved_owner=readable.owner;readable.owner=&linker;
 native_static_field_aliases::Plan denied;mapping={{new_fields[0],third_fields[0]}};assert(!denied.Prepare(hooks,metadata,second_source,&mapping,&error,&readable));
 assert(!deleted.Validate()&&method_identity::MappingOsQueries()==os_before);readable.owner=saved_owner;
 hooks.missing="GetObsoleteClass";native_static_field_aliases::Plan missing;mapping={{new_fields[0],third_fields[0]}};assert(!missing.Prepare(hooks,metadata,second_source,&mapping,&error,&readable));hooks.missing=nullptr;
 std::cout<<"Actual calibrated static field alias two-generation/metadata/retirement/history negatives PASS\n";
 return 0;
}
