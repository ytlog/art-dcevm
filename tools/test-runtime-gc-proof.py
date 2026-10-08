#!/usr/bin/env python3
"""Compile the actual Java GC proof helper and reject request-only evidence."""
import pathlib,subprocess,tempfile
root=pathlib.Path(__file__).resolve().parents[1]
source=(root/'app/src/debug/java/com/github/ytlog/artdcevm/testing/RuntimeAccessSuite.java').read_text()
start=source.index('    private static void collectAndVerifyGc(')
brace=source.index('{',start);depth=1;end=brace+1
while depth:
 if source[end]=='{':depth+=1
 elif source[end]=='}':depth-=1
 end+=1
helper=source[start:end]
with tempfile.TemporaryDirectory() as tmp:
 p=pathlib.Path(tmp);(p/'android/os').mkdir(parents=True)
 (p/'android/os/Debug.java').write_text('''package android.os;
public final class Debug {
 public static long[] counts; public static int reads;
 public static String getRuntimeStat(String key) {
  if(!key.equals("art.gc.gc-count"))throw new AssertionError("Unknown public GC oracle");
  return Long.toString(counts[Math.min(reads++,counts.length-1)]);
 }
}''')
 (p/'GcProof.java').write_text('''import java.util.HashMap;
public final class GcProof {
 static final class JSONObject extends HashMap<String,Long> {
  public JSONObject put(String key,long value){super.put(key,value);return this;}
 }
'''+helper+'''
 static void run(long[] counts, boolean expected) throws Exception {
  android.os.Debug.counts=counts;android.os.Debug.reads=0;JSONObject report=new JSONObject();
  boolean succeeded=false;
  try {collectAndVerifyGc(report,"ownedFieldDeletion");succeeded=true;}
  catch(AssertionError absent){if(expected)throw absent;}
  if(succeeded!=expected)throw new AssertionError("Request-only GC proof accepted");
  if(expected&&!Long.valueOf(1).equals(report.get("ownedFieldDeletionGcCollections")))throw new AssertionError("Wrong completed count");
  if(!expected&&!report.isEmpty())throw new AssertionError("Published unverified GC count");
 }
 public static void main(String[] args) throws Exception {
  run(new long[]{12,12,13},true);run(new long[]{12},false);run(new long[]{12,11},false);
  System.out.println("Java GC proof: delayed completion accepted; unchanged/reset counters rejected without publishing proof");
 }
}''')
 subprocess.run(['javac','-d',str(p),str(p/'android/os/Debug.java'),str(p/'GcProof.java')],check=True)
 subprocess.run(['java','-cp',str(p),'GcProof'],check=True)
