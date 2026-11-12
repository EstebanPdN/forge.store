#!/usr/bin/env python3
"""Run the production SD presentation cache on pthreads and real temp files."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
out=root/'3ds/build/gen/file-view-check';out.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='forge-file-view-check-') as temp:
 for sanitizer,label in [('address,undefined','asan-ubsan'),('thread','tsan')]:
  exe=Path(temp)/label
  subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-g','-fsanitize='+sanitizer,str(root/'3ds/tests/file-view-test.c'),'-lpthread','-o',str(exe)],check=True)
  r=subprocess.run([str(exe)],capture_output=True,text=True)
  (out/(label+'.log')).write_text(r.stdout+r.stderr)
  print(label+': '+r.stdout+r.stderr,end='');r.check_returncode()
