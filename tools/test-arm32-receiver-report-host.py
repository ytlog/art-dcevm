#!/usr/bin/env python3
"""Reject stale UUIDs/APKs, CPU substitution and fabricated/incomplete proof rows."""
import copy
import json
from arm32_receiver_report import SCOPE, validate, parse_instrumentation
run_id, sha = 'a'*32, 'b'*64
report = dict(schemaVersion=1, scope=SCOPE, runId=run_id, apkSha256=sha, passed=True,
              pid=321, sdkInt=33, is64Bit=False, privateEvolutionInitialized=False,
              publicJvmtiInitialized=False, totalOriginalCallbackHits=6, totalMatchedOwnerPrefixes=12, checks=[])
for thread, role in enumerate(('main', 'worker1', 'worker2')):
    for iteration, carrier in enumerate(('boot:Object.toString', 'app:Carrier.value')):
        report['checks'].append(dict(role=role, iteration=iteration, pid=321, tid=100+thread,
            carrier=carrier, carrierPreserved=True, proof=dict(passed=True, hookInstalled=True,
            receiverReady=True, nativeOriginalJNIReceiverReady=True, jniPrefixReadable=True,
            pointerBytes=4, runnableScopeBytes=12, jniMappedBytes=4096, originalCallbackHits=1,
            matchedOwnerPrefixes=2, privateEvolutionInitialized=False, publicJvmtiInitialized=False,
            diagnostic='')))
report['metadata'] = dict(passed=True, classProven=True, fieldsProven=True, declaredFields=3, staticFields=1, expectedFields=3, expectedStaticFields=1, pointerBytes=4, methodStride=24,
    classIndexOffset=80, declaredMethods=6, directMethods=4, totalMethods=6,
    expectedDeclaredMethods=6, expectedDirectMethods=4, dexClassDefIndex=12, dexTypeIndex=33,
    dexEntry='classes.dex', diagnostic='')
report['metadata'].update(decodedMethodIds=6,decodedFieldIds=3,methodIdRoundtrips=6,fieldIdRoundtrips=3,
    typedConsumerMethods=6,typedConsumerFields=3,distinctMethodIds=6,distinctFieldIds=3,
    namedFieldCrosschecks=3,originalJniFieldPath=True)
report['fieldFallback']=copy.deepcopy(report['metadata']);report['fieldFallback']['namedFieldCrosschecks']=0
validate(report, run_id, sha)
output = 'INSTRUMENTATION_RESULT: stream='+json.dumps(report)+'\nINSTRUMENTATION_CODE: -1\n'
parse_instrumentation(output, run_id, sha)
count = 0

def reject(value, run=run_id, digest=sha):
    global count
    try: validate(value, run, digest)
    except ValueError: count += 1; return
    raise AssertionError('Invalid proof accepted')
for key, value in [('runId','c'*32),('apkSha256','c'*64),('scope','full-cpu-support'),('is64Bit',True),
                   ('passed',False),('schemaVersion',True),('pid',True),('sdkInt',29),
                   ('privateEvolutionInitialized',True),('publicJvmtiInitialized',True),
                   ('totalOriginalCallbackHits',5),('totalMatchedOwnerPrefixes',True),('error','old error')]:
    bad = copy.deepcopy(report);bad[key]=value;reject(bad)
reject(report, 'old', sha);reject(report, run_id, 'old')
bad = copy.deepcopy(report);bad['checks'].pop();reject(bad)
for key, value in [('tid',100),('pid',True),('carrier','other'),('iteration',False),('carrierPreserved',False),('role','main')]:
    bad = copy.deepcopy(report);bad['checks'][2][key]=value;reject(bad)
for key, value in [('pointerBytes',8),('runnableScopeBytes',24),('originalCallbackHits',0),
                   ('originalCallbackHits',True),('matchedOwnerPrefixes',1),('jniMappedBytes',11),
                   ('jniMappedBytes',True),('jniMappedBytes',0x100000000),('diagnostic','refused'),
                   ('nativeOriginalJNIReceiverReady',False),('jniPrefixReadable',False),
                   ('publicJvmtiInitialized',True),('receiverReady',False)]:
    bad = copy.deepcopy(report);bad['checks'][0]['proof'][key]=value;reject(bad)
for key, value in [('passed', False), ('classProven', False), ('fieldsProven', False), ('declaredFields', 2), ('staticFields', False), ('pointerBytes', 8), ('methodStride', 32),
                   ('classIndexOffset', 76), ('declaredMethods', 5), ('directMethods', 3), ('totalMethods', 5),
                   ('expectedDeclaredMethods', True), ('dexClassDefIndex', -1), ('dexTypeIndex', True),
                   ('dexEntry', 'old.dex'), ('diagnostic', 'leaf unavailable')]:
    bad = copy.deepcopy(report);bad['metadata'][key]=value;reject(bad)
bad = copy.deepcopy(report);del bad['metadata'];reject(bad)
for key, value in [('decodedMethodIds', 5), ('decodedFieldIds', True), ('methodIdRoundtrips', 0),
                   ('fieldIdRoundtrips', 2), ('typedConsumerMethods', 5), ('typedConsumerFields', 0),
                   ('distinctMethodIds', -1), ('distinctFieldIds', 4), ('namedFieldCrosschecks', 1),
                   ('originalJniFieldPath', False)]:
    bad=copy.deepcopy(report);bad['fieldFallback'][key]=value;reject(bad)
bad=copy.deepcopy(report);del bad['fieldFallback'];reject(bad)
for text in [output.replace('CODE: -1','CODE: 0'), output+output,
             output.replace('"schemaVersion": 1','"schemaVersion": 1, "schemaVersion": 1')]:
    try: parse_instrumentation(text, run_id, sha)
    except ValueError: count += 1
    else: raise AssertionError('Ambiguous/failed instrumentation accepted')
print('ARM32 receiver report: valid six-callback proof and', count, 'negative identity/CPU/callback/process/parser cases passed')
