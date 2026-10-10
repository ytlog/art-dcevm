#!/usr/bin/env python3
"""The optional device runner must reject partial or fabricated body-only results."""
import copy
import importlib.util
from pathlib import Path
root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('public_runner', root/'tools/run-public-jvmti.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
caps = {'methodBody': True, 'evolutionAvailable': False, 'privateHooksSupported': False}
good = {'suite': 'mvp-body-evolution', 'firstCode': 0, 'schemaCode': 98, 'hierarchyCode': 98,
        'recoveryCode': 0, 'evolvedValue': 43, 'recoveredValue': 48, 'jniValue': 48,
        'reflectionValue': 48, 'retainedFieldValue': 41, 'staticValue': 113, 'checks': 4,
        'passed': True, 'capabilities': caps, 'capabilitiesAfter': dict(caps)}
module.validate_mvp_body_report(good)
rejected = 0
def refuse(value):
    global rejected
    try:
        module.validate_mvp_body_report(value)
    except RuntimeError:
        rejected += 1
        return
    raise AssertionError('partial/fabricated report accepted')
for key in good:
    bad = copy.deepcopy(good)
    del bad[key]
    refuse(bad)
for key in ('firstCode', 'recoveryCode', 'evolvedValue', 'jniValue', 'reflectionValue', 'retainedFieldValue', 'staticValue', 'checks'):
    bad = copy.deepcopy(good)
    bad[key] = True
    refuse(bad)
bad = copy.deepcopy(good)
bad['capabilitiesAfter']['evolutionAvailable'] = True
refuse(bad)
bad = copy.deepcopy(good)
bad['capabilities']['privateHooksSupported'] = True
refuse(bad)
print(f'MVP device report mandatory behavior and independent-capability gates: {rejected} negative cases passed')
