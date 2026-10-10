"""Same-process foreground evidence shared by all formal production suites."""
def validate_foreground(report, required=False):
    if not required:
        return []
    expected={'foregroundRequested':True,
              'foregroundActivity':'com.github.ytlog.artdcevm.testing.RuntimeAccessForegroundActivity',
              'foregroundResumedBefore':True,'foregroundResumedAfter':True,
              'foregroundPauseCount':0,'foregroundFinished':True}
    errors=[]
    for key,value in expected.items():
        if type(report.get(key)) is not type(value) or report.get(key)!=value:
            errors.append('Incomplete production foreground proof: '+key)
    pid=report.get('foregroundInstrumentationPid');activity_pid=report.get('foregroundActivityPid')
    if type(pid) is not int or pid<=0 or type(activity_pid) is not int or activity_pid!=pid:
        errors.append('Production foreground carrier did not prove the same live process')
    if report.get('foregroundCleanupError'):
        errors.append('Production foreground cleanup failed')
    return errors
