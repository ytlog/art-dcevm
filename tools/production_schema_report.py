"""Strict required production schema report gate; missing capabilities are failures."""
CASE_METHODS = {
    'field-types': 'fieldTypeEvolutionRequired',
    'field-staticness': 'fieldStorageEvolutionRequired',
    'method-signature': 'methodSignatureEvolutionRequired',
    'native-java': 'nativeJavaTransitionsRequired',
}
CASE_PROOFS = {
    'field-types': ('oldReflectionAndJniRetired', 'freshLongJniAndVarHandle', 'oldObjectGcState'),
    'field-staticness': ('bothStorageDirections', 'oldObjectGcState'),
    'method-signature': ('oldReflectionMethodHandleJniRetired', 'freshSignatureReflectionAndJni', 'oldObjectGcState'),
    'native-java': ('nativeJavaNative', 'cachedReflectionMethodHandleJni', 'mixedWideFpReferenceArguments', 'oldObjectGcState'),
}

def validate(report, run_id, case):
    errors = []
    for key, expected in [('runId', run_id), ('case', case), ('route', 'DcevmRuntime/NativeBridge.apply'), ('stage', 'finished')]:
        if report.get(key) != expected:
            errors.append(f'{key}: expected {expected!r}')
    if report.get('passed') is not True or report.get('sourceEngineUsed') is not False:
        errors.append('Required production proof failed or used SourceEngine')
    if report.get('capabilities', {}).get('evolutionAvailable') is not True:
        errors.append('Required production evolution capability unavailable')
    for key in CASE_PROOFS[case]:
        if report.get(key) is not True:
            errors.append(f'Missing actual proof: {key}')
    if case == 'field-staticness':
        for key, expected in [('retiredJniReadWriteChecks', 12), ('freshJniFields', 4)]:
            if type(report.get(key)) is not int or report[key] != expected:
                errors.append(f'{key}: expected {expected}')
    return errors
