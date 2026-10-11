#!/usr/bin/env python3
"""Actual RunState Authority/Verifier Domain certificate, not scanner permission."""
from pathlib import Path
import os,subprocess,sys
runner=Path(__file__).with_name('test-native-schema-run-state-boot-host.py')
subprocess.run([sys.executable,str(runner)],env=dict(os.environ,ART_DCEVM_INTERFACE_DOMAIN='1'),check=True)
