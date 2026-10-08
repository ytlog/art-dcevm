#!/usr/bin/env python3
"""Reject incomplete transaction evidence even when bootstrap probes pass."""
from copy import deepcopy
from owned_removal_report import validate_owned_removal

method = {
    'passed': True, 'runtimeServicesCalibrated': True,
    'ownedMethodDeletion': True, 'ownedMethodDeletionError': '',
    'ownedMethodDeletionJniRejected': True, 'ownedMethodDeletionReflectionRejected': True,
    'ownedMethodDeletionKeptValue': 35, 'ownedMethodDeletionIdentityPreserved': True,
    'ownedMethodHandleTargetProof': True, 'ownedMethodDeletionMethodHandleRejected': True,
    'ownedMethodDeletionMethodHandleChecks': 2, 'ownedMethodDeletionKeptMethodHandleValue': 35,
    'ownedMethodDeletionAfterGcChecks':1,'ownedMethodDeletionGcCollections':1,'ownedMethodDeletionDeclaringRootsRestored': True, 'ownedMethodDeletionCacheClearedThreads': 17,
}
field = {
    'ownedFieldDeletion': True, 'ownedFieldDeletionError': '',
    'ownedFieldDeletionJniRejected': True, 'ownedFieldDeletionReflectionRejected': True,
    'ownedFieldDeletionMethodHandleRejected': True, 'ownedFieldDeletionVarHandleRejected': True,
    'ownedFieldDeletionJniChecks': 4, 'ownedFieldDeletionReflectionChecks': 40,
    'ownedFieldDeletionMethodHandleChecks': 48, 'ownedFieldDeletionVarHandleChecks': 70,
    'ownedFieldDeletionRemovedFields': 18, 'ownedFieldDeletionKeptValue': 42,
    'ownedFieldDeletionKeptInstanceValue': 41, 'ownedFieldDeletionKeptStaticValue': 42,
    'ownedFieldDeletionIdentityPreserved': True, 'ownedFieldDeletionDeclaringRootsRestored': True,
    'ownedFieldDeletionCacheClearedThreads': 19,'ownedFieldDeletionGcCollections':1,
    'ownedFieldDeletionHotCallerJitStateBefore': 2, 'ownedFieldDeletionHotCallerWarmCalls': 100000,
    'ownedFieldDeletionDirectChecks': 5, 'ownedFieldDeletionAfterGcChecks': 3,
}
full = {**method, **field}
validate_owned_removal(method, require_fields=False)
validate_owned_removal(full, require_fields=True)

def reject(report, fields=True):
    try:
        validate_owned_removal(report, require_fields=fields)
    except RuntimeError:
        return
    raise AssertionError('Incomplete transaction report accepted')

reject({'passed': True, 'runtimeServicesCalibrated': True}, False)
reject(method)
for key in full:
    if key in ('passed', 'runtimeServicesCalibrated'):
        continue
    candidate = deepcopy(full)
    del candidate[key]
    reject(candidate)
for key, wrong in [('ownedMethodDeletion', 1), ('ownedMethodDeletionMethodHandleChecks', True),
                   ('ownedFieldDeletionJniChecks', 3), ('ownedFieldDeletionReflectionChecks', 39),
                   ('ownedFieldDeletionMethodHandleChecks', 47), ('ownedFieldDeletionVarHandleChecks', 69),
                   ('ownedFieldDeletionRemovedFields', 17), ('ownedFieldDeletionKeptInstanceValue', 0),
                   ('ownedFieldDeletionIdentityPreserved', False), ('ownedMethodDeletionCacheClearedThreads', 0),
                   ('ownedFieldDeletionCacheClearedThreads', -1), ('ownedFieldDeletionDeclaringRootsRestored', False),
                   ('ownedFieldDeletionHotCallerJitStateBefore', 1), ('ownedFieldDeletionHotCallerWarmCalls', 99999),
                   ('ownedFieldDeletionDirectChecks', 4), ('ownedFieldDeletionAfterGcChecks', 2), ('ownedMethodDeletionGcCollections',0), ('ownedFieldDeletionGcCollections',0), ('ownedFieldDeletionGcCollections',True)]:
    reject({**full, key: wrong})
print('Native removal report: bootstrap-only, missing, under-covered, mistyped and incorrect retained-state evidence rejected')
