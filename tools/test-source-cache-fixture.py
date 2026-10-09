#!/usr/bin/env python3
"""Verify real generated large DEX opcode witnesses, not reflected-only calls."""
import argparse,pathlib,re,subprocess,struct,tempfile,os
from source_cache_fixture import source_with_pair_ids,validate_pair_id_counts
p=argparse.ArgumentParser();p.add_argument('--sdk',required=True);p.add_argument('--dex',required=True);a=p.parse_args()
root=pathlib.Path(__file__).resolve().parents[1]
source=(root/'artdcevm/src/bootstrap/cache/com/github/ytlog/artdcevm/internal/SourceCacheFixture.java').read_text()
assert source_with_pair_ids(source)==source_with_pair_ids(source)
try:source_with_pair_ids(source.replace('GENERATED_CACHE_PAIR_ID_PADDING','missing'))
except ValueError:pass
else:raise AssertionError('missing generator marker accepted')
bytes_=pathlib.Path(a.dex).read_bytes();counts=validate_pair_id_counts(bytes_)
small=bytearray(bytes_)
for offset in (64,80,88):
 rejected=small.copy();struct.pack_into('<I',rejected,offset,1024)
 try:validate_pair_id_counts(rejected)
 except ValueError:pass
 else:raise AssertionError('small cache table accepted')
dump=subprocess.check_output([str(pathlib.Path(a.sdk)/'build-tools/35.0.0/dexdump'),'-d',a.dex],text=True)
warm=dump.split('SourceCacheFixture.warm:',1)[1].split('Virtual methods',1)[0]
for opcode in ('const-class','new-instance','invoke-direct','invoke-virtual','iget'):
 assert re.search(r'\b'+opcode+r'.*SourceCacheFixture',warm),opcode
assert re.search(r'invoke-virtual.*SourceCacheFixture;\.read:\(\)I',warm)
assert re.search(r'iget.*SourceCacheFixture;\.number:I',warm)
out=root/'build/evidence/cache-pair-fixture';out.mkdir(parents=True,exist_ok=True);(out/'dexdump.txt').write_text(dump)
print(f'Actual StandardDEX pair oracle PASS: type/field/method IDs={counts}; same-DEX Class/new/number/read opcodes; small-table and missing-marker refusals')

java=pathlib.Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'],text=True).strip())
with tempfile.TemporaryDirectory(prefix='cache-oracle-host-') as directory:
 work=pathlib.Path(directory);generated=work/'SourceCacheFixture.java';generated.write_text(source_with_pair_ids(source))
 host=work/'CacheOracleHost.java';host.write_text("""
import com.github.ytlog.artdcevm.internal.SourceCacheFixture;
public class CacheOracleHost {
 public static void main(String[] arguments) throws Exception {
  SourceCacheFixture fixture=new SourceCacheFixture();
  if(SourceCacheFixture.warm(fixture)!=fixture.getClass()||fixture.read()!=37)throw new AssertionError();
  java.lang.reflect.Field field=fixture.getClass().getDeclaredField("number");
  if(field.getDeclaringClass()!=fixture.getClass()||field.getType()!=int.class||field.getModifiers()!=0x41)throw new AssertionError();
  if(fixture.getClass().getDeclaredFields().length!=1026||fixture.getClass().getDeclaredMethods().length!=1027)throw new AssertionError();
  System.out.println("Ordinary Java warm/public Field/method identity PASS");
 }
}
""")
 subprocess.run([str(java/'bin/javac'),'--release','8','-d',str(work),str(generated),str(host)],check=True,timeout=60)
 result=subprocess.run([str(java/'bin/java'),'-cp',str(work),'CacheOracleHost'],capture_output=True,text=True,check=True,timeout=30)
 (out/'java-public-oracle.log').write_text(result.stdout+result.stderr);print(result.stdout.strip())
