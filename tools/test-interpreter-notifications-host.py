#!/usr/bin/env python3
"""Exercise production notification leases against failing/reentrant JVMTI setters."""
from pathlib import Path
import runpy
import shutil
import subprocess
from evidence_paths import new_evidence_directory
ROOT = Path(__file__).resolve().parents[1]
SOURCE = r'''
#include "platform/common/interpreter_notifications.h"
#include <iostream>
#include <map>
#include <cstdlib>
using artdcevm::interpreter_events::NotificationLeases;
struct Fake;
std::map<jvmtiEnv*,Fake*> environments;
void Require(bool ok) { if(!ok)std::abort(); }
struct Fake {
 jvmtiEnv env{};jvmtiInterface_1 table{};NotificationLeases leases;
 bool bp=false,cp=false,fail_cp=false,nested_watch=false,nested_commit_fail=false;
 Fake(){env.functions=&table;table.SetEventNotificationMode=Set;environments[&env]=this;}
 ~Fake(){environments.erase(&env);}
 static jvmtiError JNICALL Set(jvmtiEnv* env,jvmtiEventMode mode,jvmtiEvent event,jthread thread, ...) {
  Require(thread==nullptr);Fake& f=*environments.at(env);
  if(event==JVMTI_EVENT_CLASS_PREPARE && mode==JVMTI_ENABLE && f.fail_cp){f.fail_cp=false;return JVMTI_ERROR_INTERNAL;}
  if(event==JVMTI_EVENT_BREAKPOINT)f.bp=mode==JVMTI_ENABLE;else {Require(event==JVMTI_EVENT_CLASS_PREPARE);f.cp=mode==JVMTI_ENABLE;}
  if(event==JVMTI_EVENT_BREAKPOINT && mode==JVMTI_ENABLE && f.nested_watch){f.nested_watch=false;Require(f.leases.Watch(env,true)==0);}
  if(event==JVMTI_EVENT_BREAKPOINT && mode==JVMTI_ENABLE && f.nested_commit_fail){
   f.nested_commit_fail=false;Require(f.leases.Pin(env)==0);f.leases.Committed();return JVMTI_ERROR_INTERNAL;
  }
  return JVMTI_ERROR_NONE;
 }
};
int main(){
 {Fake f;Require(f.leases.Bound(&f.env,true)==0&&f.bp&&!f.cp);Require(f.leases.Pin(&f.env)==0&&f.bp&&f.cp);Require(f.leases.Bound(&f.env,false)==0&&f.bp&&f.cp);Require(f.leases.Release(&f.env)==0&&!f.bp&&!f.cp);}
 std::cout<<"Independent bound/continuation ownership PASS\n";
 {Fake f;Require(f.leases.Watch(&f.env,true)==0);Require(f.leases.Pin(&f.env)==0);Require(f.leases.Release(&f.env)==0&&!f.bp&&f.cp);Require(f.leases.Watch(&f.env,false)==0&&!f.cp);}
 std::cout<<"Watch lease survives rollback PASS\n";
 {Fake f;f.fail_cp=true;Require(f.leases.Pin(&f.env)==JVMTI_ERROR_INTERNAL);Require(!f.bp&&!f.cp);Require(f.leases.Pin(&f.env)==0&&f.bp&&f.cp);}
 std::cout<<"Partial activation rolls back and recovers PASS\n";
 {Fake f;Require(f.leases.Pin(&f.env)==0);f.leases.Committed();Require(f.leases.Release(&f.env)==0);Require(f.leases.Bound(&f.env,false)==0);Require(f.leases.Watch(&f.env,false)==0);Require(f.bp&&f.cp);}
 std::cout<<"Committed continuation remains pinned PASS\n";
 {Fake f;f.nested_watch=true;Require(f.leases.Bound(&f.env,true)==0&&f.bp&&f.cp);Require(f.leases.Bound(&f.env,false)==0&&!f.bp&&f.cp);}
 std::cout<<"Reentrant request reconciles latest generation PASS\n";
 {Fake f;f.nested_commit_fail=true;Require(f.leases.Bound(&f.env,true)==JVMTI_ERROR_INTERNAL);Require(f.bp&&f.cp);Require(f.leases.Release(&f.env)==0&&f.bp&&f.cp);}
 std::cout<<"Nested committed lease survives outer failure PASS\n";
 {Fake f,g;Require(f.leases.Pin(&f.env)==0);Require(f.leases.Bound(&g.env,false)==JVMTI_ERROR_INVALID_ENVIRONMENT);Require(f.bp&&f.cp&&!g.bp&&!g.cp);}
 std::cout<<"Foreign environment cannot change ownership PASS\n";
}
'''
def main():
    out = new_evidence_directory(ROOT/'build/evidence/host/interpreter-notifications')
    include = runpy.run_path(str(ROOT/'tools/test-structural-policy-host.py'))['java_home'](None)/'include'
    platform = 'darwin' if (include/'darwin').is_dir() else 'linux'
    source=out/'leases.cc';source.write_text(SOURCE);binary=out/'leases'
    subprocess.run([shutil.which('clang++') or shutil.which('c++'),'-std=c++17','-O2','-pthread','-Wall','-Wextra','-Werror','-I'+str(ROOT/'artdcevm/src/main/cpp'),'-I'+str(ROOT/'third_party/jvmti'),'-I'+str(include),'-I'+str(include/platform),str(source),'-o',str(binary)],check=True)
    result=subprocess.run([str(binary)],text=True,capture_output=True,check=True)
    (out/'results.txt').write_text(result.stdout);print(result.stdout,end='');print('Evidence: '+str(out.relative_to(ROOT)))
if __name__=='__main__':main()
