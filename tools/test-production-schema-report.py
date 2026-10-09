#!/usr/bin/env python3
import copy
from production_schema_report import CASE_METHODS,CASE_PROOFS,validate
for case in CASE_METHODS:
    report=dict(runId='a'*32,case=case,route='DcevmRuntime/NativeBridge.apply',stage='finished',passed=True,sourceEngineUsed=False,capabilities={'evolutionAvailable':True})
    report.update({key:True for key in CASE_PROOFS[case]})
    if case=='field-staticness':report.update(retiredJniReadWriteChecks=12,freshJniFields=4)
    assert not validate(report,'a'*32,case)
    for key in CASE_PROOFS[case]:
        malformed=copy.deepcopy(report);del malformed[key];assert validate(malformed,'a'*32,case)
    for change in ({'passed':False},{'passed':1},{'sourceEngineUsed':True},{'stage':'required-full-initialize.before'},{'capabilities':{'evolutionAvailable':False}},{'runId':'b'*32}):
        malformed=copy.deepcopy(report);malformed.update(change);assert validate(malformed,'a'*32,case)
print('Required schema reports: four scopes, capability failure, missing real proofs, wrong run and SourceEngine route rejected')
