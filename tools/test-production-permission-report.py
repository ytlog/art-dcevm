#!/usr/bin/env python3
import copy,json,unittest
from production_permission_report import PROOFS,VARIANTS,parse_current_report,validate
class Gate(unittest.TestCase):
 def test_all_paths_and_negative_proofs(self):
  for case,variants in VARIANTS.items():
   for scope in ('full','common-owned'):
    report={'runId':'a'*32,'case':case,'initializationScope':scope,'route':'DcevmRuntime/NativeBridge.apply','stage':'finished','passed':True,'sourceEngineUsed':False,'capabilities':{'evolutionAvailable':True,'backend':'runtime-capabilities','partialEvolutionAvailable':True},'applies':[{'variant':v,'code':0} for v in variants],**dict.fromkeys(PROOFS[case],True)}
    self.assertEqual([],validate(report,'a'*32,case,scope))
    self.assertEqual(report,parse_current_report(json.dumps(report),'a'*32,case,scope))
    for key in PROOFS[case]:
     bad=copy.deepcopy(report);bad.pop(key);self.assertTrue(validate(bad,'a'*32,case,scope))
    for index in range(len(variants)):
     for code in (False,1,None):
      bad=copy.deepcopy(report);bad['applies'][index]['code']=code;self.assertTrue(validate(bad,'a'*32,case,scope))
    for key,value in [('passed',False),('sourceEngineUsed',True),('runId','b'*32),('stage','scenario.before'),('applies',[])]:
     bad=copy.deepcopy(report);bad[key]=value;self.assertTrue(validate(bad,'a'*32,case,scope))
    with self.assertRaises(ValueError):parse_current_report(json.dumps(report),'b'*32,case,scope)
if __name__=='__main__':unittest.main()
