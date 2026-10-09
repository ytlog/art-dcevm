#!/usr/bin/env python3
"""Execute the real phase logger on a JVM with a recording Android Log boundary."""
import json,subprocess,tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
javac=Path('/Library/Java/JavaVirtualMachines/temurin-21.jdk/Contents/Home/bin/javac');java=javac.with_name('java')
with tempfile.TemporaryDirectory() as directory:
    tmp=Path(directory);(tmp/'android/os').mkdir(parents=True);(tmp/'android/util').mkdir(parents=True)
    (tmp/'android/os/Process.java').write_text('package android.os; public final class Process {public static int myPid(){return 731;}}')
    (tmp/'android/util/Log.java').write_text('package android.util; public final class Log {public static int i(String tag,String value){if(!tag.equals("ArtDcevmPhase"))throw new AssertionError(tag);System.out.println(value);return 0;}}')
    (tmp/'PhaseLogHost.java').write_text('''import com.github.ytlog.artdcevm.testing.TestPhaseLog;
public final class PhaseLogHost {
 public static void main(String[] args){
  TestPhaseLog log=new TestPhaseLog("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
  log.mark("foreground.start-activity.before");log.mark("foreground.start-activity.after");
  new TestPhaseLog("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb").mark("method-deletion.before");
  try {new TestPhaseLog("old-run");throw new AssertionError("Malformed run accepted");}catch(IllegalArgumentException expected){}
  try {log.mark("bad\\\"phase");throw new AssertionError("Malformed phase accepted");}catch(IllegalArgumentException expected){}
 }
}''')
    command=[str(javac),'-J-Duser.language=en','-source','8','-target','8','-d',str(tmp/'classes'),str(tmp/'android/os/Process.java'),str(tmp/'android/util/Log.java'),str(root/'app/src/debug/java/com/github/ytlog/artdcevm/testing/TestPhaseLog.java'),str(tmp/'PhaseLogHost.java')]
    subprocess.run(command,check=True,capture_output=True,text=True)
    result=subprocess.run([str(java),'-cp',str(tmp/'classes'),'PhaseLogHost'],check=True,capture_output=True,text=True)
    reports=[json.loads(l) for l in result.stdout.splitlines()]
    assert reports==[{'runId':'a'*32,'pid':731,'phase':'foreground.start-activity.before'},
                     {'runId':'a'*32,'pid':731,'phase':'foreground.start-activity.after'},
                     {'runId':'b'*32,'pid':731,'phase':'method-deletion.before'}],reports
print('Phase logger: actual JVM before/after markers, current UUID/PID and malformed-input rejection passed')
