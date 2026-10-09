#!/usr/bin/env python3
"""Install two exact APKs and run an isolated nondebuggable actual-JIT frame oracle."""
import argparse,hashlib,json,pathlib,subprocess,sys,uuid
from apk_verification import require_install_success,base_apk_path,verified_apk_sha256
from evidence_paths import new_evidence_directory
from frameproof_report import validate_frameproof

def main():
    parser=argparse.ArgumentParser()
    for name in ('serial','adb','apk','test-apk'):parser.add_argument('--'+name,required=True)
    args=parser.parse_args();root=pathlib.Path(__file__).resolve().parents[1]
    out=new_evidence_directory(root/'build/evidence/nondebuggable-frameproof','run')
    run_id=uuid.uuid4().hex;report={'passed':False,'serial':args.serial,'runId':run_id,
        'scope':'owned nondebuggable JIT carrier and two inlinees, six live vregs and native/DEXPC evidence; no full evolution'}
    def adb(label,*command,timeout=120):
        result=subprocess.run([args.adb,'-s',args.serial,*command],text=True,capture_output=True,timeout=timeout)
        (out/(label+'.log')).write_text(result.stdout+result.stderr);return result
    try:
        for key,path,package in [('application',args.apk,'dev.artdcevm.frameproof'),('test',args.test_apk,'dev.artdcevm.frameproof.test')]:
            expected=hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest();report[key+'RequestedSha256']=expected
            result=adb(key+'-install','install','-r',path);require_install_success(result.returncode,result.stdout,result.stderr)
            paths=adb(key+'-path','shell','pm','path',package);paths.check_returncode()
            hashed=adb(key+'-hash','shell','sha256sum',base_apk_path(paths.stdout));hashed.check_returncode()
            report[key+'Sha256']=verified_apk_sha256(hashed.stdout,expected)
        stopped=adb('stop','shell','am','force-stop','dev.artdcevm.frameproof');stopped.check_returncode()
        result=adb('instrumentation','shell','am','instrument','-w','-r','-e','frameRunId',run_id,
            'dev.artdcevm.frameproof.test/com.github.ytlog.artdcevm.frameproof.FrameProofInstrumentation',timeout=180)
        result.check_returncode();report['proof']=validate_frameproof(result.stdout,run_id);report['passed']=True
    except (OSError,RuntimeError,ValueError,subprocess.SubprocessError) as error:report['error']=str(error)
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));print(out)
    return 0 if report['passed'] else 1
if __name__=='__main__':sys.exit(main())
