"""Bounded, read-only current-run crash capture before instrumentation cleanup."""
import json,re,subprocess,time
from test_phase_file import parse_phase_file


def correlated_crash_lines(text,pids):
    """Keep target-PID Java messages and complete target native tombstone blocks."""
    lines=text.splitlines();kept=[];block=[]
    def flush():
        if block and any(re.search(r'\bpid:\s*'+str(pid)+r',.*>>>\s*dev\.artdcevm\.lab\s*<<<',line) for pid in pids for line in block):
            kept.extend(block)
        block.clear()
    native_emitter=None
    for line in lines:
        match=re.match(r'^\d\d-\d\d\s+\S+\s+(\d+)\s+\d+\s+[A-Z]\s+(\S+):',line)
        emitter=int(match.group(1)) if match else None
        tag=match.group(2) if match else None
        if '*** *** ***' in line and tag=='DEBUG':
            flush();native_emitter=emitter;block.append(line);continue
        if block and emitter==native_emitter and tag=='DEBUG':block.append(line);continue
        # A different logger must never be included inside the target tombstone.
        if block:flush();native_emitter=None
        if emitter in pids:kept.append(line)
    flush()
    return kept


def collect_crash_diagnostics(adb,serial,directory,run_id,budget=10):
    deadline=time.monotonic()+min(max(budget,0),10)
    summary={'runId':run_id,'commands':{},'phases':[],'correlatedPids':[]}
    def capture(label,args,cap):
        remaining=deadline-time.monotonic()
        if remaining<=0:summary['commands'][label]={'error':'Diagnostic budget exhausted'};return ''
        try:
            result=subprocess.run([adb,'-s',serial,*args],capture_output=True,text=True,timeout=min(cap,remaining))
            summary['commands'][label]={'exitCode':result.returncode}
            return result.stdout if result.returncode==0 else ''
        except (OSError,subprocess.SubprocessError) as error:
            summary['commands'][label]={'error':str(error)};return ''
    if re.fullmatch('[0-9a-f]{32}',run_id):
        # A dead process has no live PID; the strict UUID phase file proves its PID.
        text=capture('phase-file',['shell','run-as','dev.artdcevm.lab','cat','files/runtime-phases/'+run_id+'.json'],2)
        summary['phases']=parse_phase_file(text,run_id,[])
        if summary['phases']:
            (directory/'crash-phase-file.json').write_text(text)
        else:
            logs=capture('phase-logcat',['logcat','-d','-t','128','-s','ArtDcevmPhase:I'],2)
            for line in logs.splitlines():
                match=re.search(r'\{.*\}\s*$',line)
                if not match:continue
                try:phase=json.loads(match.group(0))
                except ValueError:continue
                if (isinstance(phase,dict) and set(phase)=={'runId','pid','phase'} and phase['runId']==run_id
                    and type(phase['pid']) is int and 0<phase['pid']<=2147483647
                    and isinstance(phase['phase'],str) and re.fullmatch('[a-z0-9.-]{1,64}',phase['phase'])):
                    summary['phases'].append(phase)
    summary['phases']=summary['phases'][-64:]
    summary['correlatedPids']=list(dict.fromkeys(p['pid'] for p in summary['phases']))[:2]
    if summary['phases']:summary['lastPhase']=summary['phases'][-1]
    logs=capture('crash-logcat',['logcat','-b','crash','-d','-t','256','-v','threadtime'],4)
    lines=correlated_crash_lines(logs,summary['correlatedPids'])
    summary['crashLines']=len(lines)
    if lines:
        path=directory/'crash-logcat.log';path.write_text('\n'.join(lines)+'\n');summary['crashLog']=str(path)
    (directory/'crash-diagnostics.json').write_text(json.dumps(summary,indent=2)+'\n')
    return summary
