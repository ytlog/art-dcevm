"""Bounded read-only snapshots before owned test timeout cleanup."""
import json,re,subprocess,time
from test_phase_file import parse_phase_file

def collect_timeout_diagnostics(adb,serial,directory,run_id,budget=15):
    deadline=time.monotonic()+budget
    summary={'runId':run_id,'livePids':[],'commands':{},'phases':[]}
    def capture(label,args,cap):
        remaining=deadline-time.monotonic()
        if remaining<=0:
            summary['commands'][label]={'error':'Diagnostic budget exhausted'};return ''
        path=directory/('timeout-'+label+'.log')
        try:
            result=subprocess.run([adb,'-s',serial,*args],capture_output=True,text=True,timeout=min(cap,remaining))
            output=result.stdout+result.stderr
            summary['commands'][label]={'exitCode':result.returncode,'log':str(path)}
            path.write_text(output)
            return result.stdout if result.returncode==0 else ''
        except (OSError,subprocess.SubprocessError) as error:
            def decoded(value):return value.decode(errors='replace') if isinstance(value,bytes) else value or ''
            path.write_text(decoded(getattr(error,'stdout',None))+decoded(getattr(error,'stderr',None))+'\n'+str(error))
            summary['commands'][label]={'error':str(error),'log':str(path)};return ''
    pids=capture('pid',['shell','pidof','dev.artdcevm.lab'],2).split()
    summary['livePids']=[int(p) for p in pids if p.isdigit() and int(p)>0][:2]
    if re.fullmatch('[0-9a-f]{32}',run_id):
        text=capture('phase-file',['shell','run-as','dev.artdcevm.lab','cat','files/runtime-phases/'+run_id+'.json'],2)
        summary['phases']=parse_phase_file(text,run_id,summary['livePids'])
    for pid in summary['livePids']:
        rows=capture('threads-'+str(pid),['shell','ps','-T','-p',str(pid),'-o','PID,TID,COMM,STAT,WCHAN'],3)
        candidates=[]
        for row in rows.splitlines():
            parts=row.split()
            if len(parts)>=4 and parts[0]==str(pid) and parts[1].isdigit():
                # COMM is the kernel thread name. NAME was argv and silently
                # selected only the main thread. Prioritize actual running and
                # recorded Java marker TIDs, then bounded remaining threads.
                tid=int(parts[1]);name=parts[2].lower();state=parts[3]
                candidates.append((0 if state.startswith('R') or 'instrument' in name or 'junit' in name else 1,tid))
        phase_tids=[p['threadId'] for p in reversed(summary['phases']) if p.get('pid')==pid and type(p.get('threadId')) is int]
        tids=list(dict.fromkeys(phase_tids+[tid for _,tid in sorted(candidates)]+[pid]))[:24]
        files=['/proc/%d/status'%pid]
        for tid in tids:files.extend(['/proc/%d/task/%d/comm'%(pid,tid),'/proc/%d/task/%d/status'%(pid,tid),'/proc/%d/task/%d/wchan'%(pid,tid)])
        capture('state-'+str(pid),['shell','run-as','dev.artdcevm.lab','cat',*files],3)
    if summary['phases']:summary['commands']['phase-file']['parsed']=True
    logs=capture('phases',['logcat','-d','-s','ArtDcevmPhase:I'],4)
    for line in logs.splitlines():
        if 'ArtDcevmPhase' not in line:continue
        match=re.search(r'\{.*\}\s*$',line)
        if not match:continue
        try:phase=json.loads(match.group(0))
        except json.JSONDecodeError:continue
        if not isinstance(phase,dict) or phase.get('runId')!=run_id:continue
        pid=phase.get('pid');name=phase.get('phase')
        if type(pid) is not int or pid<=0 or not isinstance(name,str) or not re.fullmatch('[a-z0-9.-]+',name):continue
        if summary['livePids'] and pid not in summary['livePids']:continue
        if not summary['commands'].get('phase-file',{}).get('parsed'):
            summary['phases'].append(phase)
    if summary['phases']:summary['lastPhase']=summary['phases'][-1]
    (directory/'timeout-diagnostics.json').write_text(json.dumps(summary,indent=2)+'\n')
    return summary
