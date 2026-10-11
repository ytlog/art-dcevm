"""Shared independent graph fixture setup; no source extraction or legacy tools."""
from pathlib import Path
import os, subprocess

def prepare(root:Path,out:Path):
    out.mkdir(parents=True,exist_ok=True)
    java=Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'],text=True).strip())
    platform='darwin' if os.uname().sysname=='Darwin' else 'linux'
    return java,platform
