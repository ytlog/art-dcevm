"""Validate this run's formal production method transaction evidence."""
import json
import re

def validate_production_bootstrap(instrumentation, device_log, run_id, *, require_fields=False, require_foreground=False):
    code = re.findall(r'^INSTRUMENTATION_CODE:\s*(-?\d+)\s*$', instrumentation, re.M)
    if code != ['-1'] or 'OK (1 test)' not in instrumentation or 'FAILURES!!!' in instrumentation:
        raise RuntimeError('Production instrumentation did not finish exactly one successful test')
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
    report = reports[0]
    expected = {'productionBootstrap': True, 'formalApplyMethodDeletion': True,
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
    if require_foreground:
        foreground={'foregroundRequested':True,
                    'foregroundActivity':'com.github.ytlog.artdcevm.testing.RuntimeAccessForegroundActivity',
                    'foregroundResumedBefore':True,'foregroundResumedAfter':True,
                    'foregroundPauseCount':0,'foregroundFinished':True}
        for key,value in foreground.items():
            if type(report.get(key)) is not type(value) or report.get(key)!=value:
                raise RuntimeError(f'Incomplete production foreground proof: {key}={report.get(key)!r}')
        pid=report.get('foregroundInstrumentationPid');activity_pid=report.get('foregroundActivityPid')
        if type(pid) is not int or pid<=0 or type(activity_pid) is not int or activity_pid!=pid:
            raise RuntimeError('Production foreground carrier did not prove the same live process')
    capabilities = report.get('capabilities')
    if not isinstance(capabilities, dict) or capabilities.get('nativeMethodDeletion') is not True:
        raise RuntimeError('Production method deletion capability was not reported')
    if require_fields and capabilities.get('fieldDeletion') is not True:
        raise RuntimeError('Production field deletion capability was not reported')
    return report
