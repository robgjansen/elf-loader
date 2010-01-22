#!/usr/bin/env python
import sys
import subprocess

cmd = ['valgrind',
       '--leak-check=full',
       '--show-reachable=yes',
       '--track-origins=yes',
       '--error-exitcode=2']
cmd += sys.argv[1:]

val = subprocess.Popen(cmd, 
                       stdout = subprocess.PIPE,
                       stderr = subprocess.PIPE)
(stdout, stderr) = val.communicate()
if val.returncode != 0 or "== LEAK SUMMARY:" in stderr:
    sys.exit(1)

