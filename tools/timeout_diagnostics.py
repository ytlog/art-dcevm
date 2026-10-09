"""Bounded read-only snapshots before owned test timeout cleanup."""
import json,re,subprocess,time

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
    for pid in summary['livePids']:
        rows=capture('threads-'+str(pid),['shell','ps','-T','-p',str(pid),'-o','PID,TID,NAME,STAT,WCHAN'],3)
        tids=[pid]
        for row in rows.splitlines():
            parts=row.split()
            if len(parts)>=3 and parts[0]==str(pid) and parts[1].isdigit() and ('instrument' in parts[2].lower() or 'junit' in parts[2].lower()):
                tids.append(int(parts[1]))
        files=['/proc/%d/status'%pid]
        for tid in dict.fromkeys(tids):files.extend(['/proc/%d/task/%d/status'%(pid,tid),'/proc/%d/task/%d/wchan'%(pid,tid)])
        capture('state-'+str(pid),['shell','run-as','dev.artdcevm.lab','cat',*files],3)
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
        summary['phases'].append(phase)
    if summary['phases']:summary['lastPhase']=summary['phases'][-1]
    (directory/'timeout-diagnostics.json').write_text(json.dumps(summary,indent=2)+'\n')
    return summary
