"""Strict independent nondebuggable frame target evidence; no evolution claim."""
import json,re
from owned_removal_report import validate_active_frames

def validate_frameproof(output,run_id):
    codes=re.findall(r'^INSTRUMENTATION_CODE:\s*(-?\d+)\s*$',output,re.M)
    if codes!=['-1']:
        raise RuntimeError('Missing unique successful current instrumentation completion')
    payloads=re.findall(r'(?:^|\n)(?:INSTRUMENTATION_RESULT: stream=)?FRAME_PROOF_REPORT=([^\n\r]+)',output)
    if len(payloads)!=1:
        raise RuntimeError('Missing unique current structured nondebuggable frame report')
    report=json.loads(payloads[0])
    if type(report) is not dict or report.get('runId')!=run_id or type(run_id) is not str or not run_id:
        raise RuntimeError('Frame report belongs to a different run')
    expected={'passed':True,'protocol':1,'targetNondebuggable':True,'instrumentationNondebuggable':True,'sameSignature':True}
    for key,value in expected.items():
        if type(report.get(key)) is not type(value) or report[key]!=value:
            raise RuntimeError('Missing nondebuggable process proof: '+key)
    bootstrap=report.get('bootstrap')
    if type(bootstrap) is not dict:raise RuntimeError('Missing pure-JNI bootstrap proof')
    expected={'passed':True,'runtimeJavaDebuggableObserved':True,'runtimeJavaDebuggable':False,
        'runtimeReceiversMatched':True,'methodIdentitiesChecked':5,'carrierJitCompiled':True,
        'publicJvmtiInitialized':False,'warmCalls':100000,'error':''}
    for key,value in expected.items():
        if type(bootstrap.get(key)) is not type(value) or bootstrap[key]!=value:
            raise RuntimeError('Missing independent native bootstrap proof: '+key)
    for key in ('enqueueCalls','compileCompletionCalls','heapCollectionCalls','gcCompletionDelta'):
        if type(bootstrap.get(key)) is not int or bootstrap[key]<=0:
            raise RuntimeError('Missing actual observation/completion count: '+key)
    frame=report.get('activeFrames')
    validate_active_frames({'activeFramesRequested':True,'ownedActiveFrameProof':frame})
    for key,value in {'runtimeJavaDebuggableObserved':True,'runtimeJavaDebuggable':False}.items():
        if type(frame.get(key)) is not type(value) or frame[key]!=value:
            raise RuntimeError('Frame proof runtime debug policy changed: '+key)
    return report
