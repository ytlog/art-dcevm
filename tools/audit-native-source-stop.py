#!/usr/bin/env python3
"""Audit fixed AOSP GC exclusion and final suspend handoff; keep downloads ignored."""
import argparse,base64,hashlib,json,re,subprocess
from pathlib import Path
REFS=[f'android-{n}.0.0_r1' for n in range(11,17)]+['b753cf97923c3695338d21466fa14c57b480a59a']
FILES=['runtime/gc/heap.cc','runtime/gc/heap.h','runtime/gc/collector_type.h','runtime/gc/gc_cause.h','runtime/gc/collector/gc_type.h','runtime/thread_list.cc','openjdkjvmti/ti_redefine.cc']
def clean(s):return re.sub(r'/\*.*?\*/|//[^\n]*','',s,flags=re.S)
def body(text,signature):
 m=re.search(signature,text);assert m,signature
 start=text.index('{',m.end());depth=1;end=start+1
 while depth:
  assert end<len(text),signature
  depth+=(text[end]=='{')-(text[end]=='}');end+=1
 return text[start+1:end-1]
def ordinal(text,enum,name):
 b=body(clean(text),r'\benum\s+'+enum+r'\b');items=[x.strip().split('=')[0].strip() for x in b.split(',') if x.strip()]
 assert all('=' not in x for x in b.split(',')),enum
 return items.index(name)
def audit(source):
 heap=clean(source['runtime/gc/heap.cc']);header=clean(source['runtime/gc/heap.h']);redefine=clean(source['openjdkjvmti/ti_redefine.cc'])
 assert ordinal(source['runtime/gc/collector_type.h'],'CollectorType','kCollectorTypeMS')==1
 assert ordinal(source['runtime/gc/gc_cause.h'],'GcCause','kGcCauseInstrumentation')==8
 assert ordinal(source['runtime/gc/collector/gc_type.h'],'GcType','kGcTypeNone')==0
 start=body(heap,r'void Heap::StartGC\(')
 for phrase in ['WaitForGcToCompleteLocked(cause, self)','collector_type_running_ = collector_type','thread_running_gc_ = self']:assert phrase in start
 moving=body(header,r'static bool IsMovingGc\(');assert 'kCollectorTypeMS' not in moving and 'kCollectorTypeCC' in moving
 finish=body(heap,r'void Heap::FinishGC\(')
 assert 'collector_type_running_ = kCollectorTypeNone' in finish and 'thread_running_gc_ = nullptr' in finish and 'Broadcast(self)' in finish
 # Every collection counter/metric update must be inside a real-collection branch.
 residue=re.sub(r'if\s*\(gc_type != collector::kGcTypeNone\)\s*\{','GC_IF{',finish)
 while 'GC_IF{' in residue:
  pos=residue.index('GC_IF{');content=body(residue[pos:],r'GC_IF');residue=residue[:pos]+residue[pos+len('GC_IF{')+len(content)+1:]
 assert not any(x in residue for x in ['GetCurrentGcIteration','last_gc_type_','gc_count_last_window_','blocking_gc_count_','gcs_completed_.fetch_add'])
 inc=body(heap,r'void Heap::IncrementDisableMovingGC\(')
 assert re.search(r'if\s*\(IsMovingGc\(collector_type_running_\)\)\s*\{\s*WaitForGcToCompleteLocked',inc)
 final=redefine.index('art::ScopedSuspendAll ssa("Final installation of redefined Classes!",')
 collection=redefine.rfind('if (!CollectAndCreateNewInstances(holder))',0,final)
 assert collection>=0
 interval=redefine[collection:final]
 assert interval.index('RegisterExistingDexCache')>0 and interval.index('UnregisterAllBreakpoints')>0
 assert 'ScopedDisableConcurrentAndMovingGc sdcamgc' in interval and 'art::ScopedThreadSuspension sts' in interval
 assert not any(x in interval for x in ['StartGC(','ScopedGCCriticalSection'])
 # Ordinary original final suppression waits only moving collectors, not MS sentinel.
 disable=body(redefine,r'class ScopedDisableConcurrentAndMovingGc')
 assert 'IncrementDisableMovingGC' in disable and 'DecrementDisableMovingGC' in disable
 assert 'StartGC' not in disable and 'ScopedGCCriticalSection' not in disable
 assert 'suspend_all_count_' in source['runtime/thread_list.cc']
parser=argparse.ArgumentParser();parser.add_argument('--offline',action='store_true');parser.add_argument('--source-dir',type=Path,default=Path('build/evidence/wholeprocess-source/seven-family'));args=parser.parse_args()
manifest=[]
for ref in REFS:
 source={}
 for name in FILES:
  path=args.source_dir/ref/name
  if not args.offline:
   url=f'https://android.googlesource.com/platform/art/+/{ref}/{name}?format=TEXT'
   path.parent.mkdir(parents=True,exist_ok=True)
   path.write_bytes(base64.b64decode(subprocess.check_output(['/usr/bin/curl','--fail','--silent','--show-error','--retry','2',url])))
  raw=path.read_bytes();source[name]=raw.decode();manifest.append(dict(ref=ref,path=name,sha256=hashlib.sha256(raw).hexdigest()))
 audit(source);print(ref,'GC exclusion/MS nonmoving/FinishNone/source final ordering PASS')
(args.source_dir/'audited-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
