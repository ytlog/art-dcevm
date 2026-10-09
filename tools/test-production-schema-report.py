#!/usr/bin/env python3
import copy
from production_schema_report import CASE_METHODS,CASE_PROOFS,validate
for case in CASE_METHODS:
    report=dict(runId='a'*32,case=case,route='DcevmRuntime/NativeBridge.apply',stage='finished',passed=True,sourceEngineUsed=False,capabilities={'evolutionAvailable':True},lastApplyCode=0)
    report.update({key:True for key in CASE_PROOFS[case]})
    if case=='field-staticness':report.update(retiredJniReadWriteChecks=12,freshJniFields=4)
    if case=='hierarchy-addition':report.update(successfulApplyCount=2,interfaceJniChecks=6)
    if case=='hierarchy-narrowing':report.update(successfulApplyCount=3)
    if case=='hierarchy-narrowing':report.update(illegalTypedStorageChecks=3,illegalStorageCode0=98,illegalStorageCode1=98,illegalStorageCode2=98)
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
print('Required schema reports: six cases, capability failure, missing real proofs, wrong run and SourceEngine route rejected')

case='hierarchy-narrowing'
report=dict(runId='a'*32,case=case,route='DcevmRuntime/NativeBridge.apply',stage='finished',passed=True,sourceEngineUsed=False,capabilities={'evolutionAvailable':True},lastApplyCode=0,successfulApplyCount=3,illegalTypedStorageChecks=3,illegalStorageCode0=98,illegalStorageCode1=98,illegalStorageCode2=98)
report.update({key:True for key in CASE_PROOFS[case]})
for change in ({'successfulApplyCount':True},{'successfulApplyCount':2},{'illegalTypedStorageChecks':True},{'illegalTypedStorageChecks':2},{'illegalStorageCode0':0},{'illegalStorageCode1':False},{'illegalStorageCode2':None}):
    malformed=copy.deepcopy(report);malformed.update(change);assert validate(malformed,'a'*32,case)

case='hierarchy-addition'
report=dict(runId='a'*32,case=case,route='DcevmRuntime/NativeBridge.apply',stage='finished',passed=True,sourceEngineUsed=False,capabilities={'evolutionAvailable':True},lastApplyCode=0,successfulApplyCount=2,interfaceJniChecks=6)
report.update({key:True for key in CASE_PROOFS[case]})
for change in ({'successfulApplyCount':True},{'successfulApplyCount':1},{'interfaceJniChecks':True},{'interfaceJniChecks':5}):
    malformed=copy.deepcopy(report);malformed.update(change);assert validate(malformed,'a'*32,case)
