"""Strict required production schema report gate; missing capabilities are failures."""
import json
from production_foreground_report import validate_foreground

def parse_current_report(text, run_id, case, scope):
    report = json.loads(text)
    if not isinstance(report, dict):
        raise ValueError('Schema report is not an object')
    for key, expected in [('runId', run_id), ('case', case),
                          ('initializationScope', scope),
                          ('route', 'DcevmRuntime/NativeBridge.apply')]:
        if report.get(key) != expected:
            raise ValueError('Current schema report identity mismatch: ' + key)
    return report

CASE_METHODS = {
    'field-types': 'fieldTypeEvolutionRequired',
    'field-staticness': 'fieldStorageEvolutionRequired',
    'method-signature': 'methodSignatureEvolutionRequired',
    'native-java': 'nativeJavaTransitionsRequired',
    'hierarchy-addition': 'hierarchyAdditionRequired',
    'hierarchy-narrowing': 'hierarchyNarrowingRequired',
}
CASE_PROOFS = {
    'hierarchy-addition': ('actualInterfaceAndParentAddition', 'inheritedFieldsPreservedAndAddedZeroed', 'cachedVirtualReflectionMethodHandleJni', 'interfaceDispatch', 'oldObjectAndSubclassGcIdentity', 'constructorsNotReplayed'),
    'hierarchy-narrowing': ('legalTypedStorageNarrowing', 'retiredAncestorReceiverChecks', 'unrelatedAncestorReceiverStillWorks', 'illegalTypedStorageAtomicRejection', 'legalNarrowingAfterRejectedAttempts', 'oldObjectGcState'),
    'field-types': ('oldReflectionAndJniRetired', 'freshLongJniAndVarHandle', 'oldObjectGcState'),
    'field-staticness': ('bothStorageDirections', 'oldObjectGcState'),
    'method-signature': ('oldReflectionMethodHandleJniRetired', 'freshSignatureReflectionAndJni', 'oldObjectGcState'),
    'native-java': ('nativeJavaNative', 'cachedReflectionMethodHandleJni', 'mixedWideFpReferenceArguments', 'oldObjectGcState'),
}

def validate(report, run_id, case, scope="full", require_foreground=False):
    errors = validate_foreground(report,require_foreground)
    for key, expected in [('runId', run_id), ('case', case), ('route', 'DcevmRuntime/NativeBridge.apply'), ('stage', 'finished')]:
        if report.get(key) != expected:
            errors.append(f'{key}: expected {expected!r}')
    if report.get('passed') is not True or report.get('sourceEngineUsed') is not False:
        errors.append('Required production proof failed or used SourceEngine')
    if scope not in ('full', 'common-owned') or report.get('initializationScope', 'full') != scope:
        errors.append('Required initialization scope mismatch')
    capabilities = report.get('capabilities', {})
    if scope == 'full':
        if capabilities.get('evolutionAvailable') is not True:
            errors.append('Required production evolution capability unavailable')
    elif capabilities.get('backend') != 'runtime-capabilities' or capabilities.get('partialEvolutionAvailable') is not True:
        errors.append('Required shared runtime owner unavailable')
    if type(report.get('lastApplyCode')) is not int or report['lastApplyCode'] != 0:
        errors.append('Missing successful actual schema apply')
    for key in CASE_PROOFS[case]:
        if report.get(key) is not True:
            errors.append(f'Missing actual proof: {key}')
    if case == 'field-staticness':
        for key, expected in [('retiredJniReadWriteChecks', 12), ('freshJniFields', 4)]:
            if type(report.get(key)) is not int or report[key] != expected:
                errors.append(f'{key}: expected {expected}')
    if case in ('hierarchy-addition', 'hierarchy-narrowing'):
        expected = 2 if case == 'hierarchy-addition' else 3
        if type(report.get('successfulApplyCount')) is not int or report['successfulApplyCount'] != expected:
            errors.append('Missing actual complete hierarchy apply sequence')
    if case == 'hierarchy-addition':
        if type(report.get('interfaceJniChecks')) is not int or report['interfaceJniChecks'] != 6:
            errors.append('Missing six actual cached JNI interface checks')
    if case == 'hierarchy-narrowing':
        if type(report.get('illegalTypedStorageChecks')) is not int or report['illegalTypedStorageChecks'] != 3:
            errors.append('Missing three actual illegal retained storage checks')
        for index in range(3):
            value = report.get('illegalStorageCode' + str(index))
            if type(value) is not int or value == 0:
                errors.append('Missing refused illegal storage apply: ' + str(index))
    return errors
