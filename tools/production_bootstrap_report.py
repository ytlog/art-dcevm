"""Validate this run's formal production method transaction evidence."""
import json
from production_foreground_report import validate_foreground
import re

SOURCE_EPISODES = {
    'initializer': ('initializerEpisodeObserved', 'initializerEpisodeDiagnostic'),
    'compiled-frames': ('compiledFramesCalibrated', 'frameCalibrationDiagnostic'),
    'receiver-invoke': ('receiverInvokeSourceCalibrated', 'receiverInvokeSourceDiagnostic'),
}

def _require_success(instrumentation, self_instrumentation=False):
    code = re.findall(r'^INSTRUMENTATION_CODE:\s*(-?\d+)\s*$', instrumentation, re.M)
    if self_instrumentation:
        completion=re.findall(r'^INSTRUMENTATION_RESULT: bootstrapRunnerCompleted=(.*)$',instrumentation,re.M)
        if code != ['-1'] or completion != ['true'] or 'FAILURES!!!' in instrumentation or re.search(r'OK \(\d+ tests?\)',instrumentation):
            raise RuntimeError('Production self instrumentation did not finish the complete shared suite')
        return
    if 'bootstrapRunnerCompleted=' in instrumentation:
        raise RuntimeError('Self completion cannot establish the default JUnit route')
    if code != ['-1'] or len(re.findall(r'^(?:INSTRUMENTATION_RESULT: stream=)?OK \(1 test\)\s*$', instrumentation,re.M)) != 1 or 'FAILURES!!!' in instrumentation:
        raise RuntimeError('Production instrumentation did not finish exactly one successful test')

def validate_production_bootstrap_payload(instrumentation, persisted, run_id, *, require_fields=False, require_foreground=False, self_instrumentation=False, require_source_episodes=()):
    """Full current-run atomic payload, independent of logcat's per-record size limit."""
    _require_success(instrumentation,self_instrumentation)
    def unique_keys(pairs):
        result={}
        for key,value in pairs:
            if key in result:raise ValueError('Duplicate production report key: '+key)
            result[key]=value
        return result
    report=json.loads(persisted,object_pairs_hook=unique_keys)
    if not isinstance(report,dict) or report.get('runId')!=run_id:
        raise RuntimeError('Missing current-run persisted production transaction report')
    return _validate_report(report,require_fields=require_fields,require_foreground=require_foreground, require_source_episodes=require_source_episodes)

def validate_production_bootstrap(instrumentation, device_log, run_id, *, require_fields=False, require_foreground=False, require_source_episodes=()):
    """Legacy log reader kept for evidence checks; device runner uses the complete file."""
    _require_success(instrumentation)
    reports = []
    for line in device_log.splitlines():
        if 'ArtDcevmProduction' not in line:
            continue
        text = re.search(r'\{.*\}\s*$', line)
        if not text:
            continue
        try:
            candidate = json.loads(text.group(0))
        except json.JSONDecodeError:
            continue
        if isinstance(candidate, dict) and candidate.get('runId') == run_id:
            reports.append(candidate)
    if len(reports) != 1:
        raise RuntimeError('Missing or ambiguous current-run production transaction report')
    return _validate_report(reports[0],require_fields=require_fields,require_foreground=require_foreground, require_source_episodes=require_source_episodes)

def _validate_report(report, *, require_fields=False, require_foreground=False, require_source_episodes=()):
    expected = {'lastApplyCode': 0, 'productionBootstrap': True, 'formalApplyMethodDeletion': True,
                'cachedJniRejectedModes': 4, 'cachedReflectionRejected': True,
                'retainedIdentity': True, 'retainedCompute': 35, 'retainedValue': 31}
    if require_fields:
        expected.update({'formalApplyFieldDeletion': True, 'fieldHandleRejectedPaths': 8,
                         'fieldJniRejectedPaths': 36, 'fieldReflectionRejectedPaths': 36,
                         'fieldFreshKeptJniChecks': 4,
                         'fieldRetainedValue': 41, 'fieldStaticRetainedValue': 42,
                         'fieldRetainedCompute': 42, 'fieldRetainedIdentity': True})
    for key, value in expected.items():
        observed = report.get(key)
        if type(observed) is not type(value) or observed != value:
            raise RuntimeError(f'Incomplete formal method deletion: {key}={observed!r}')
    foreground_errors=validate_foreground(report,require_foreground)
    if foreground_errors:raise RuntimeError('; '.join(foreground_errors))
    capabilities = report.get('capabilities')
    if not isinstance(capabilities, dict) or capabilities.get('nativeMethodDeletion') is not True:
        raise RuntimeError('Production method deletion capability was not reported')
    if require_fields and capabilities.get('fieldDeletion') is not True:
        raise RuntimeError('Production field deletion capability was not reported')
    for episode in require_source_episodes:
        if episode not in SOURCE_EPISODES:
            raise ValueError('Unknown required source episode: '+str(episode))
        proven, diagnostic = SOURCE_EPISODES[episode]
        if capabilities.get(proven) is not True or capabilities.get(diagnostic) != '':
            raise RuntimeError(f'Incomplete source calibration {episode}: '
                               f'{proven}={capabilities.get(proven)!r}, '
                               f'{diagnostic}={capabilities.get(diagnostic)!r}')
    return report
