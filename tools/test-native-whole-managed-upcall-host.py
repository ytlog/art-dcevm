#!/usr/bin/env python3
"""Whole original-walker integration using real linked ELF declarations.
ART/JNI are named host leaf models; real JVM calibration is independently tested.
"""
from pathlib import Path
import os,subprocess,sys
runner=Path(__file__).with_name('test-native-schema-run-state-boot-host.py')
env=dict(os.environ,ART_DCEVM_WHOLE_UPCALL='1')
subprocess.run([sys.executable,str(runner)],env=env,check=True)
