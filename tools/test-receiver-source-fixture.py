#!/usr/bin/env python3
"""Check each generated invocation opcode and independent Java reflection result."""
import argparse,pathlib,re,subprocess,tempfile,os
p=argparse.ArgumentParser();p.add_argument('--sdk',required=True);p.add_argument('--dex',required=True);a=p.parse_args()
root=pathlib.Path(__file__).resolve().parents[1];out=root/'build/evidence/receiver-source-fixture';out.mkdir(parents=True,exist_ok=True)
dump=subprocess.check_output([str(pathlib.Path(a.sdk)/'build-tools/35.0.0/dexdump'),'-d',a.dex],text=True);(out/'dexdump.txt').write_text(dump)
from receiver_source_fixture import validate_receiver_dump
validate_receiver_dump(dump)
try:validate_receiver_dump(dump.replace("invoke-super/range", "invoke-static/range"))
except ValueError:pass
else:raise AssertionError("changed actual opcode accepted")
java=pathlib.Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'],text=True).strip())
with tempfile.TemporaryDirectory() as d:
 work=pathlib.Path(d);host=work/'ReceiverOracleHost.java';host.write_text('''import com.github.ytlog.artdcevm.internal.ReceiverSourceFixture;
public class ReceiverOracleHost { public static void main(String[] args) throws Exception {
 Object object=new ReceiverSourceFixture.Receiver();
 String[] kinds={"Virtual","Super","Direct","Static","Interface"}; int[] values={11,21,41,51,31};
 Class<?>[] owners={ReceiverSourceFixture.Base.class,ReceiverSourceFixture.Base.class,ReceiverSourceFixture.Receiver.class,ReceiverSourceFixture.class,ReceiverSourceFixture.Contract.class};
 for(int k=0;k<5;k++)for(int r=0;r<2;r++) {String suffix=r==0?"Small":"Range";
 Class<?> callerOwner=k==1||k==2?ReceiverSourceFixture.Receiver.class:ReceiverSourceFixture.class;
 Class<?>[] callerArgs=k==0?new Class<?>[]{ReceiverSourceFixture.Base.class}:k==4?new Class<?>[]{ReceiverSourceFixture.Contract.class}:new Class<?>[0];
 java.lang.reflect.Method caller=callerOwner.getDeclaredMethod("call"+kinds[k]+suffix,callerArgs);
 Class<?>[] targetArgs=r==0?new Class<?>[]{int.class}:new Class<?>[]{int.class,int.class,int.class,int.class,int.class,int.class};
 java.lang.reflect.Method target=owners[k].getDeclaredMethod(kinds[k].toLowerCase()+suffix,targetArgs);
 if(target.getDeclaringClass()!=owners[k]||target.getReturnType()!=int.class)throw new AssertionError();
 Object result=caller.invoke(k==1||k==2?object:null,callerArgs.length==0?new Object[0]:new Object[]{object});
 if(!result.equals(values[k]+r*20))throw new AssertionError(result);
 } System.out.println("Ten public reflection callers/declaring target identities and original Java result oracles PASS"); }}''')
 source=root/'artdcevm/src/bootstrap/receiver/com/github/ytlog/artdcevm/internal/ReceiverSourceFixture.java';subprocess.run([str(java/'bin/javac'),'--release','8','-d',str(work),str(source),str(host)],check=True);r=subprocess.run([str(java/'bin/java'),'-cp',str(work),'ReceiverOracleHost'],capture_output=True,text=True,check=True);(out/'java-oracle.log').write_text(r.stdout+r.stderr);print(r.stdout.strip())
print('Actual D8 DEX: all ten exact ordinary/range invocation opcodes PASS')
