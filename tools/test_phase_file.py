"""Strict bounded current-run app phase file decoder; no device access here."""
import json,re
MAX_BYTES=32768
MAX_PHASES=64

def parse_phase_file(text,run_id,live_pids):
    if not isinstance(run_id,str) or not re.fullmatch('[0-9a-f]{32}',run_id):return []
    if not isinstance(text,str) or len(text)>MAX_BYTES:return []
    try:
        if len(text.encode('utf-8'))>MAX_BYTES:return []
    except UnicodeError:return []
    try:
        def unique_pairs(pairs):
            result={}
            for key,value in pairs:
                if key in result:raise ValueError('Duplicate phase JSON member')
                result[key]=value
            return result
        data=json.loads(text,object_pairs_hook=unique_pairs)
    except (ValueError,UnicodeError,RecursionError):return []
    if not isinstance(data,dict) or set(data)!={'runId','pid','phases'} or data['runId']!=run_id:return []
    pid=data['pid']
    if type(pid) is not int or pid<=0 or pid>2147483647 or (live_pids and pid not in live_pids):return []
    entries=data['phases']
    if not isinstance(entries,list) or len(entries)>MAX_PHASES:return []
    result=[];previous=None
    for item in entries:
        if not isinstance(item,dict) or set(item)!={'phase','sequence','threadId'}:return []
        phase,seq,tid=item['phase'],item['sequence'],item['threadId']
        if not isinstance(phase,str) or not 1<=len(phase)<=64 or not re.fullmatch('[a-z0-9.-]+',phase):return []
        if type(seq) is not int or not 0<=seq<=9223372036854775807 or (previous is not None and seq!=previous+1):return []
        if type(tid) is not int or tid<=0 or tid>2147483647:return []
        previous=seq
        result.append({'runId':run_id,'pid':pid,'phase':phase,'threadId':tid,'sequence':seq})
    return result
