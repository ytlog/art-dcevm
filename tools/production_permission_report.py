"""Strict current-run production final/access gate, including every apply result."""
import json
CASE_METHODS={'final-fields':'finalFieldsRequired','field-access':'fieldAccessRequired'}
PROOFS={
 'final-fields':('finalDeniedCachedAndFresh','foreignFinalWritesDenied','cachedJniFinalWritesLegal','mutableRestoredAllFourPaths','oldObjectGcState'),
 'field-access':('foreignPublicReadWriteAllowed','foreignPrivateCachedAndFreshDenied','ordinaryReflectionAndFreshLookupDenied','cachedAuthorizedFourPathsRetained','publicRestored','oldObjectGcState')}
VARIANTS={'final-fields':['modifier-field-final','modifier-field-original'],
          'field-access':['private-field-public','private-field-private','private-field-public']}
def parse_current_report(text,run_id,case,scope):
 report=json.loads(text)
 if not isinstance(report,dict):raise ValueError('Permission report is not an object')
 for key,value in [('runId',run_id),('case',case),('initializationScope',scope),('route','DcevmRuntime/NativeBridge.apply')]:
  if report.get(key)!=value:raise ValueError('Current permission identity mismatch: '+key)
 return report
def validate(report,run_id,case,scope='full'):
 errors=[]
 for key,value in [('runId',run_id),('case',case),('initializationScope',scope),('route','DcevmRuntime/NativeBridge.apply'),('stage','finished')]:
  if report.get(key)!=value:errors.append('Mismatch: '+key)
 if report.get('passed') is not True or report.get('sourceEngineUsed') is not False:errors.append('Production proof failed or used SourceEngine')
 caps=report.get('capabilities',{})
 if not isinstance(caps,dict):caps={}
 if scope=='full':
  if caps.get('evolutionAvailable') is not True:errors.append('Production evolution unavailable')
 elif scope!='common-owned' or caps.get('backend')!='runtime-capabilities' or caps.get('partialEvolutionAvailable') is not True:errors.append('Shared owner unavailable')
 applies=report.get('applies')
 expected=VARIANTS.get(case,[])
 if not isinstance(applies,list) or len(applies)!=len(expected) or not expected:errors.append('Missing apply sequence')
 else:
  for result,variant in zip(applies,expected):
   if not isinstance(result,dict) or result.get('variant')!=variant or type(result.get('code')) is not int or result['code']!=0:errors.append('Unsuccessful actual apply: '+variant)
 for key in PROOFS.get(case,()):
  if report.get(key) is not True:errors.append('Missing actual proof: '+key)
 return errors
