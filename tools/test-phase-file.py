#!/usr/bin/env python3
"""Check strict run/PID/sequence identity and bounded app phase input."""
import copy,json
from test_phase_file import parse_phase_file
rid='a'*32
base={'runId':rid,'pid':731,'phases':[{'phase':'jit-services.before','sequence':42,'threadId':745},{'phase':'jit-services.after','sequence':43,'threadId':745}]}
assert len(parse_phase_file(json.dumps(base),rid,[731]))==2
assert len(parse_phase_file(json.dumps(base),rid,[]))==2
assert parse_phase_file(json.dumps(base),'b'*32,[731])==[]
assert parse_phase_file(json.dumps(base),rid,[732])==[]
for key,value in [('pid',True),('pid',0),('pid',731.0),('runId',False),('phases',{}),('phases',[{}])]:
    bad=copy.deepcopy(base);bad[key]=value;assert parse_phase_file(json.dumps(bad),rid,[731])==[],bad
for key,value in [('phase','bad"phase'),('phase','a'*65),('threadId',True),('threadId',-1),('sequence',True),('sequence',43.0),('sequence',41)]:
    bad=copy.deepcopy(base);bad['phases'][1][key]=value;assert parse_phase_file(json.dumps(bad),rid,[731])==[],bad
bad=copy.deepcopy(base);bad['phases']*=33;assert parse_phase_file(json.dumps(bad),rid,[731])==[]
for text in [chr(0xd800),'x'*32769,'{','\\ud800','['*2000+']'*2000,json.dumps(base).replace('"pid": 731','"pid": 731, "pid": 731')]:
    assert parse_phase_file(text,rid,[731])==[]
assert parse_phase_file(json.dumps(base)+' '*32769,rid,[731])==[]
print('Phase file: current run/PID/types, duplicate keys, malformed/deep/oversized JSON and contiguous bounded sequence passed')
