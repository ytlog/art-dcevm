#!/usr/bin/env python3
"""Run real JVM logger and concurrently parse bounded atomic app phase files."""
import json,subprocess,tempfile,time
from pathlib import Path
from test_phase_file import parse_phase_file
root=Path(__file__).resolve().parents[1]
javac=Path('/Library/Java/JavaVirtualMachines/temurin-21.jdk/Contents/Home/bin/javac');java=javac.with_name('java')
with tempfile.TemporaryDirectory() as directory:
    tmp=Path(directory);(tmp/'android/os').mkdir(parents=True);(tmp/'android/util').mkdir(parents=True)
    (tmp/'android/os/Process.java').write_text('package android.os; public final class Process {public static int myPid(){return 731;} public static int myTid(){return 745;}}')
    (tmp/'android/util/Log.java').write_text('package android.util; public final class Log {public static int i(String tag,String value){if(!tag.equals("ArtDcevmPhase"))throw new AssertionError(tag);System.out.println(value);return 0;} public static int w(String tag,String value,Throwable error){System.err.println(value);return 0;}}')
    (tmp/'PhaseLogHost.java').write_text('''import com.github.ytlog.artdcevm.testing.TestPhaseLog;
import java.io.File;
public final class PhaseLogHost {
 public static void main(String[] args)throws Exception{
  File files=new File(args[0]);
  TestPhaseLog log=new TestPhaseLog("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",files);
  log.mark("foreground.start-activity.before");log.mark("foreground.start-activity.after");
  new TestPhaseLog("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",files).mark("method-deletion.before");
  try {new TestPhaseLog("old-run",files);throw new AssertionError("Malformed run accepted");}catch(IllegalArgumentException expected){}
  try {log.mark("bad\\\"phase");throw new AssertionError("Malformed phase accepted");}catch(IllegalArgumentException expected){}
  try {log.mark(new String(new char[65]).replace('\\0','a'));throw new AssertionError("Long phase accepted");}catch(IllegalArgumentException expected){}
  TestPhaseLog writer=new TestPhaseLog("cccccccccccccccccccccccccccccccc",files);
  for(int i=0;i<80;i++){writer.mark("native-step-"+i);Thread.sleep(2);}
  // Storage rejection must preserve the original logcat marker and suite flow.
  File blocked=new File(files,"blocked");blocked.createNewFile();
  new TestPhaseLog("dddddddddddddddddddddddddddddddd",blocked).mark("io-fallback");
  File history=new File(files,"history");
  for(int i=1;i<=11;i++)new TestPhaseLog(String.format("%032x",i),history);
  new TestPhaseLog("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee",files).mark("old-marker");
  new TestPhaseLog("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee",files).mark("reset-marker");
 }
}''')
    subprocess.run([str(javac),'-J-Duser.language=en','-source','8','-target','8','-d',str(tmp/'classes'),str(tmp/'android/os/Process.java'),str(tmp/'android/util/Log.java'),str(root/'app/src/debug/java/com/github/ytlog/artdcevm/testing/TestPhaseLog.java'),str(tmp/'PhaseLogHost.java')],check=True,capture_output=True,text=True)
    files=tmp/'files';files.mkdir()
    with (tmp/'stdout').open('w') as stdout,(tmp/'stderr').open('w') as stderr:
        process=subprocess.Popen([str(java),'-cp',str(tmp/'classes'),'PhaseLogHost',str(files)],stdout=stdout,stderr=stderr)
        reads=0;target=files/'runtime-phases'/('c'*32+'.json');end=time.monotonic()+15
        while process.poll() is None:
            if time.monotonic()>end:process.kill();raise AssertionError('JVM phase test exceeded budget')
            if target.exists():
                text=target.read_text();data=json.loads(text)
                assert data['runId']=='c'*32 and data['pid']==731
                assert len(data['phases'])<=64 and len(text.encode())<=32768
                parsed=parse_phase_file(text,'c'*32,[731]);assert len(parsed)==len(data['phases']);reads+=1
            time.sleep(.001)
        assert process.returncode==0,(tmp/'stderr').read_text()
    reports=[json.loads(line) for line in (tmp/'stdout').read_text().splitlines()]
    assert reports[:3]==[{'runId':'a'*32,'pid':731,'phase':'foreground.start-activity.before'},
                     {'runId':'a'*32,'pid':731,'phase':'foreground.start-activity.after'},
                     {'runId':'b'*32,'pid':731,'phase':'method-deletion.before'}]
    assert any(r['phase']=='io-fallback' for r in reports) and reads>10
    phases=parse_phase_file(target.read_text(),'c'*32,[731])
    assert len(phases)==64 and phases[0]['sequence']==16 and phases[-1]['phase']=='native-step-79'
    assert phases[-1]['threadId']==745
    assert parse_phase_file((files/'runtime-phases'/('a'*32+'.json')).read_text(),'b'*32,[731])==[]
    assert not list(files.rglob('*.tmp'))
    assert len(list((files/'history/runtime-phases').glob('*.json')))==8
    reset=parse_phase_file((files/'runtime-phases'/('e'*32+'.json')).read_text(),'e'*32,[731])
    assert len(reset)==1 and reset[0]['phase']=='reset-marker' and reset[0]['sequence']==0
print('Phase logger: real JVM synced atomic files, concurrent valid reads, bounded current UUID/TID history and logcat I/O fallback passed')
