"""Compatibility entry point for the canonical Quest pipeline."""
import argparse
import subprocess
import sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--quest',action='store_true')
parser.add_argument('--adb')
parser.add_argument('--serial')
args=parser.parse_args()
options=['auto' if args.quest else 'build']
if args.adb: options += ['--adb',args.adb]
if args.serial: options += ['--serial',args.serial]
raise SystemExit(subprocess.run([sys.executable,'-I',str(ROOT/'scripts/quest/manage.py'),*options],cwd=ROOT).returncode)
