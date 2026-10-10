#!/usr/bin/env python3
"""Independent census test reuses the single aggregate fixture producer.
Real services execute; original ART/JNI leaf bodies remain explicit unit doubles.
"""
import os
import subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1]
# Reuse generation/compiler source inventory, in an independent evidence dir.
source=(root/'tools/test-native-schema-run-state-host.py').read_text()
source=source.replace("out=root/'build/evidence/host/native-schema-run-state'", "out=root/'build/evidence/host/native-cohort-root-census'")
prefix=source.split('units=')[0]
namespace={'__file__':str(root/'tools/test-native-schema-run-state-host.py'),'__name__':'__main__'}
exec(compile(prefix,'shared-aggregate-fixture','exec'),namespace)
out=namespace['out']
baseline=(root/'tests/host/native_schema_run_state_test.cc').read_text()
head,body=baseline.split('int main(){',1)
(out/'cohort_fixture.inc').write_text(head)
setup=body.split('  native_schema_run_state::CacheCalibration witness',1)[0]
setup=setup.replace('native_typed_heap_walk::Service typed_heap;CalibrateSchemaTypedHeap(owner_hooks,', 'CensusHooks census_hooks;native_typed_heap_walk::Service typed_heap;CalibrateSchemaTypedHeap(census_hooks,')
(out/'cohort_setup.inc').write_text('int main(){\n'+setup)
tail='units='+source.split('units=',1)[1]
tail=tail.replace("root/'tests/host/native_schema_run_state_test.cc'", "root/'tests/host/native_cohort_root_census_test.cc'")
tail=tail.replace("exe=out/'native_schema_run_state_test'", "exe=out/'native_cohort_root_census_test'")
exec(compile(tail,'shared-aggregate-compiler','exec'),namespace)

# Independently calibrated original source HandleScope declarations: legacy
# count-only and used-size. No API/version chooses the declaration in Service.
r=subprocess.run([str(namespace['exe'])],capture_output=True,text=True,env=dict(os.environ,SOURCE_SCOPE_SIZED='1',ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
(out/'sized-result.log').write_text(r.stdout+r.stderr);r.check_returncode();print('Used-size source HandleScope: '+r.stdout.strip())
