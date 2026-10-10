#!/usr/bin/env python3
"""Run the actual registry TU with calibrated metadata/root owners, before graph-only JNI fixtures."""
import os
from pathlib import Path
import runpy
os.environ['SOURCE_REGISTRY_ONLY']='1'
runpy.run_path(str(Path(__file__).with_name('test-native-hierarchy-pipeline-host.py')),run_name='__main__')
