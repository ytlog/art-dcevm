"""Strict proof gate for self-instrumented original ARM32 JNI receivers."""
import json
import re
SCOPE = 'arm32-original-jni-readonly-metadata'
COMPONENT = 'dev.artdcevm.lab/com.github.ytlog.artdcevm.testing.Arm32ReceiverInstrumentation'

def strict_json(text):
    if len(text) > 65536:
        raise ValueError("Oversized native proof report")
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError('Duplicate JSON field: ' + key)
            result[key] = value
        return result
    return json.loads(text, object_pairs_hook=pairs, parse_constant=lambda value: (_ for _ in ()).throw(ValueError('Nonfinite JSON')))

def validate_metadata(proof):
    if not isinstance(proof, dict) or proof.get('passed') is not True or proof.get('classProven') is not True or proof.get('fieldsProven') is not True or proof.get('diagnostic') != '':
        raise ValueError('Complete ARM32 Method/Class/Field metadata proof missing')
    for field in ('pointerBytes', 'methodStride', 'classIndexOffset', 'declaredMethods', 'directMethods', 'totalMethods', 'expectedDeclaredMethods', 'expectedDirectMethods', 'dexClassDefIndex', 'dexTypeIndex', 'declaredFields', 'staticFields', 'expectedFields', 'expectedStaticFields'):
        if type(proof.get(field)) is not int:
            raise ValueError('Invalid metadata observation: ' + field)
    if proof['pointerBytes'] != 4 or proof['methodStride'] not in (24, 28) or proof['classIndexOffset'] not in (72, 80):
        raise ValueError('Wrong fixed ARM32 metadata declaration')
    if proof['declaredMethods'] != 6 or proof['expectedDeclaredMethods'] != 6 or proof['directMethods'] != 4 or proof['expectedDirectMethods'] != 4 or proof['totalMethods'] < 6:
        raise ValueError('Complete static/private/virtual/native/constructor table not proved')
    if proof['declaredFields'] != 3 or proof['expectedFields'] != 3 or proof['staticFields'] != 1 or proof['expectedStaticFields'] != 1:
        raise ValueError('Complete static/private/instance Field table not proved')
    if proof['dexClassDefIndex'] < 0 or proof['dexTypeIndex'] < 0 or not re.fullmatch(r'classes(?:[2-9]|[1-9][0-9]+)?\.dex', proof.get('dexEntry', '')):
        raise ValueError('Independent installed APK class identity missing')

def validate(report, run_id, apk_sha256):
    if not re.fullmatch('[0-9a-f]{32}', run_id) or not re.fullmatch('[0-9a-f]{64}', apk_sha256):
        raise ValueError('Expected strict run UUID/APK SHA-256')
    if not isinstance(report, dict) or report.get('schemaVersion') != 1 or type(report.get('schemaVersion')) is not int:
        raise ValueError('Wrong report schema')
    if report.get('runId') != run_id or report.get('apkSha256') != apk_sha256 or report.get('scope') != SCOPE:
        raise ValueError('Stale/wrong APK or proof scope')
    if report.get('passed') is not True or report.get('is64Bit') is not False or report.get('privateEvolutionInitialized') is not False or report.get('publicJvmtiInitialized') is not False or 'error' in report:
        raise ValueError('Actual ARM32 isolated proof missing')
    if type(report.get('pid')) is not int or report['pid'] <= 0 or type(report.get('sdkInt')) is not int or report['sdkInt'] < 30:
        raise ValueError('Invalid process/system identity')
    rows = report.get('checks')
    if not isinstance(rows, list) or len(rows) != 6:
        raise ValueError('Need two real carrier checks on each of three threads')
    tids = {}
    hits = prefixes = 0
    for index, row in enumerate(rows):
        role, iteration = ('main', 'worker1', 'worker2')[index // 2], index % 2
        if not isinstance(row, dict) or row.get('role') != role or type(row.get('iteration')) is not int or row['iteration'] != iteration or type(row.get('pid')) is not int or row['pid'] != report['pid']:
            raise ValueError('Wrong check/process/carrier ordering')
        tid = row.get('tid')
        if type(tid) is not int or tid <= 0 or (role in tids and tids[role] != tid):
            raise ValueError('Invalid thread proof')
        tids[role] = tid
        if row.get('carrier') != ('boot:Object.toString', 'app:Carrier.value')[iteration] or row.get('carrierPreserved') is not True or 'error' in row:
            raise ValueError('Owned carrier preservation failed')
        proof = row.get('proof')
        if not isinstance(proof, dict) or any(proof.get(name) is not True for name in ('passed', 'hookInstalled', 'receiverReady', 'nativeOriginalJNIReceiverReady', 'jniPrefixReadable')):
            raise ValueError('Original JNI receiver callback unavailable')
        if proof.get('privateEvolutionInitialized') is not False or proof.get('publicJvmtiInitialized') is not False:
            raise ValueError('Wrong initialization scope')
        for field, expected in [('pointerBytes', 4), ('runnableScopeBytes', 12), ('originalCallbackHits', 1), ('matchedOwnerPrefixes', 2)]:
            if type(proof.get(field)) is not int or proof[field] != expected:
                raise ValueError('Missing native width/callback observation: ' + field)
        if type(proof.get('jniMappedBytes')) is not int or not 12 <= proof['jniMappedBytes'] <= 0xffffffff:
            raise ValueError('Readable JNI prefix not proved')
        if proof.get('diagnostic') != '':
            raise ValueError('Native proof reported a refusal diagnostic')
        hits += proof['originalCallbackHits']; prefixes += proof['matchedOwnerPrefixes']
    if len(set(tids.values())) != 3:
        raise ValueError('Worker callbacks reused one native thread')
    for field, value in [('totalOriginalCallbackHits', hits), ('totalMatchedOwnerPrefixes', prefixes)]:
        if type(report.get(field)) is not int or report[field] != value:
            raise ValueError('Invalid observed callback totals')
    metadata = report.get('metadata')
    for proof in (metadata, report.get('fieldFallback')):
        validate_metadata(proof)
        if not isinstance(proof, dict) or proof.get('passed') is not True or proof.get('originalJniFieldPath') is not True or proof.get('diagnostic') != '':
            raise ValueError('Real original JNI Field/typed source path missing')
        for key, expected in [('decodedMethodIds', 6), ('decodedFieldIds', 3), ('methodIdRoundtrips', 6), ('fieldIdRoundtrips', 3), ('typedConsumerMethods', 6), ('typedConsumerFields', 3)]:
            if type(proof.get(key)) is not int or proof[key] != expected:
                raise ValueError('Opaque-ID roundtrip/typed observation missing: ' + key)
        for key, maximum in [('distinctMethodIds', 6), ('distinctFieldIds', 3), ('namedFieldCrosschecks', 3)]:
            if type(proof.get(key)) is not int or not 0 <= proof[key] <= maximum:
                raise ValueError('Invalid ID/source observation: ' + key)
    if report['fieldFallback']['namedFieldCrosschecks'] != 0:
        raise ValueError('Original JNI fallback used the optional named Field leaf')
    return report

def parse_instrumentation(output, run_id, apk_sha256):
    payloads = re.findall(r'^INSTRUMENTATION_RESULT: stream=(.*)$', output, re.M)
    codes = re.findall(r'^INSTRUMENTATION_CODE: (.*)$', output, re.M)
    if len(payloads) != 1 or codes != ['-1']:
        raise ValueError('Missing/unambiguous successful instrumentation result')
    return validate(strict_json(payloads[0]), run_id, apk_sha256)
