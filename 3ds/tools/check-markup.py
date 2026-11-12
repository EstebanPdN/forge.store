#!/usr/bin/env python3
"""Sanitizer checks for the production Markdown/HTML reader and layout modules."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2];out=root/'3ds/build/gen/markup-check';out.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='forge-markup-check-') as temp:
 for name,sources in [('parser',['gh_markup','gh_tables']),('flow',['gh_flow']),('tables',['gh_markup','gh_tables','gh_flow','gh_table_layout']),('releases',['gh_markup','gh_tables','gh_releases'])]:
  test={'parser':'markup_test','flow':'flow_test','tables':'table_layout_test','releases':'releases_test'}[name]
  exe=Path(temp)/name;cmd=['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers','-g','-fsanitize=address,undefined','-I','/opt/homebrew/include']+[str(root/'3ds/source'/f'{s}.c') for s in sources]+[str(root/'3ds/tests/markup'/f'{test}.c'),'-o',str(exe)]
  if name=='releases':cmd+=['-L','/opt/homebrew/lib','-ljansson']
  subprocess.run(cmd,check=True);r=subprocess.run([str(exe)],capture_output=True,text=True)
  (out/(name+'.log')).write_text(r.stdout+r.stderr);print(name+': '+r.stdout+r.stderr,end='');r.check_returncode()
