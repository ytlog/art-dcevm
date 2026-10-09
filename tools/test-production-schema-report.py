#!/usr/bin/env python3
import copy
from production_schema_report import CASE_METHODS,CASE_PROOFS,validate
for case in CASE_METHODS:
    report=dict(runId='a'*32,case=case,route='DcevmRuntime/NativeBridge.apply',stage='finished',passed=True,sourceEngineUsed=False,capabilities={'evolutionAvailable':True},lastApplyCode=0)
    report.update({key:True for key in CASE_PROOFS[case]})
    if case=='field-staticness':report.update(retiredJniReadWriteChecks=12,freshJniFields=4)
    assert not validate(report,'a'*32,case)
    for key in CASE_PROOFS[case]:
        malformed=copy.deepcopy(report);del malformed[key];assert validate(malformed,'a'*32,case)
    for change in ({'lastApplyCode':None},{'lastApplyCode':98},{'lastApplyCode':False},{'initializationScope':'common-owned'},{'passed':False},{'passed':1},{'sourceEngineUsed':True},{'stage':'required-full-initialize.before'},{'capabilities':{'evolutionAvailable':False}},{'runId':'b'*32}):
        malformed=copy.deepcopy(report);malformed.update(change);assert validate(malformed,'a'*32,case)
    common=copy.deepcopy(report)
    common.update(initializationScope='common-owned',capabilities={'backend':'runtime-capabilities','partialEvolutionAvailable':True,'evolutionAvailable':False})
    assert not validate(common,'a'*32,case,'common-owned')
    assert validate(common,'a'*32,case)
    assert validate(report,'a'*32,case,'common-owned')
    for change in ({'capabilities':{'backend':'source13','partialEvolutionAvailable':True}}, {'capabilities':{'backend':'runtime-capabilities','partialEvolutionAvailable':False}}, {'initializationScope':'full'}, {'lastApplyCode':98}, {'sourceEngineUsed':True}):
        malformed=copy.deepcopy(common);malformed.update(change);assert validate(malformed,'a'*32,case,'common-owned')
print('Required schema reports: four scopes, capability failure, missing real proofs, wrong run and SourceEngine route rejected')
