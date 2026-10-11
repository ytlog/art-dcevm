#!/usr/bin/env python3
"""Actual complete whole logical inline exception chain with current Plan roots."""
from pathlib import Path
import os, subprocess, sys
runner=Path(__file__).with_name('test-native-schema-run-state-boot-host.py')
env=dict(os.environ,ART_DCEVM_WHOLE_UPCALL='1',ART_DCEVM_INLINE_UPCALL='1')
subprocess.run([sys.executable,str(runner)],env=env,check=True)
