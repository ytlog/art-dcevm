"""Require observed native deletion results, independently of bootstrap calibration."""

def validate_owned_removal(report, *, require_fields):
    expected = {
        'ownedMethodDeletion': True,
        'ownedMethodDeletionError': '',
        'ownedMethodDeletionJniRejected': True,
        'ownedMethodDeletionReflectionRejected': True,
        'ownedMethodDeletionKeptValue': 35,
        'ownedMethodDeletionIdentityPreserved': True,
        'ownedMethodHandleTargetProof': True,
        'ownedMethodDeletionMethodHandleRejected': True,
        'ownedMethodDeletionMethodHandleChecks': 2,
        'ownedMethodDeletionKeptMethodHandleValue': 35,
        'ownedMethodDeletionDeclaringRootsRestored': True,
        'ownedMethodDeletionAfterGcChecks': 1,
    }
    if require_fields:
        expected.update({
            'ownedFieldDeletion': True,
            'ownedFieldDeletionError': '',
            'ownedFieldDeletionJniRejected': True,
            'ownedFieldDeletionReflectionRejected': True,
            'ownedFieldDeletionMethodHandleRejected': True,
            'ownedFieldDeletionVarHandleRejected': True,
            'ownedFieldDeletionJniChecks': 4,
            'ownedFieldDeletionReflectionChecks': 40,
            'ownedFieldDeletionMethodHandleChecks': 48,
            'ownedFieldDeletionVarHandleChecks': 70,
            'ownedFieldDeletionRemovedFields': 18,
            'ownedFieldDeletionKeptValue': 42,
            'ownedFieldDeletionKeptInstanceValue': 41,
            'ownedFieldDeletionKeptStaticValue': 42,
            'ownedFieldDeletionIdentityPreserved': True,
            'ownedFieldDeletionDeclaringRootsRestored': True,
            'ownedFieldDeletionHotCallerJitStateBefore': 2,
            'ownedFieldDeletionDirectChecks': 5,
            'ownedFieldDeletionAfterGcChecks': 3,
        })
    for key, value in expected.items():
        observed = report.get(key)
        # Python bool compares equal to 1; counts and booleans must have the
        # correct JSON type as well as the independently required fixture value.
        if type(observed) is not type(value) or observed != value:
            raise RuntimeError(f'Missing or incomplete actual deletion proof: {key}={observed!r}; expected {value!r}')
    prefixes = ['ownedMethodDeletion'] + (['ownedFieldDeletion'] if require_fields else [])
    for prefix in prefixes:
        key = prefix + 'CacheClearedThreads'
        value = report.get(key)
        if type(value) is not int or value <= 0:
            raise RuntimeError(f'Missing synchronous final transaction cache clearing proof: {key}={value!r}')
        key = prefix + 'GcCollections'
        value = report.get(key)
        if type(value) is not int or value <= 0:
            raise RuntimeError(f'Missing completed GC before cached access: {key}={value!r}')
    if require_fields:
        value = report.get('ownedFieldDeletionHotCallerWarmCalls')
        if type(value) is not int or value < 100000:
            raise RuntimeError(f'Missing compiled field caller warmup proof: ownedFieldDeletionHotCallerWarmCalls={value!r}')


def validate_active_frames(report):
    """An independent owned-fixture scope; no general recovery capability."""
    if report.get('activeFramesRequested') is not True:
        raise RuntimeError('Missing current requested active-frame execution marker')
    proof = report.get('ownedActiveFrameProof')
    if type(proof) is not dict:
        raise RuntimeError('Missing structured owned active-frame proof')
    expected = {
        'passed': True, 'carrierJitCompiled': True,
        'inlineFlagNativeObserved': True, 'blockedLiveWitnesses': True,
        'retainedLiveWitnesses': True, 'frames': 3, 'integers': 3,
        'references': 3, 'locationReads': 6, 'dexPcChecks': 3,
        'warmCalls': 100000, 'error': '',
    }
    for key, value in expected.items():
        observed = proof.get(key)
        if type(observed) is not type(value) or observed != value:
            raise RuntimeError(f'Missing or incomplete actual active-frame proof: {key}={observed!r}; expected {value!r}')
