#!/usr/bin/python3
"""Local frontend to the same package worker used by USB web management."""
import argparse
import json
import sys
import reader_manager as manager

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('action', choices=['status', 'check', 'install', 'remove'])
parser.add_argument('package', nargs='?', default='')
args = parser.parse_args()
try:
    if args.action == 'status':
        result = manager.status()
    else:
        result = manager.start('online' if args.action == 'install' else args.action, args.package)
    print(json.dumps(result))
except (ValueError, OSError) as error:
    print(str(error), file=sys.stderr)
    raise SystemExit(1)
