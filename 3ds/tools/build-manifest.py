#!/usr/bin/env python3
"""Hash the actual work builds after packaging, never a previous binary."""
from pathlib import Path
import hashlib,json
root=Path(__file__).resolve().parents[1]/'build'
report={name:{'bytes':(root/name).stat().st_size,'sha256':hashlib.sha256((root/name).read_bytes()).hexdigest()} for name in ['forge-store.cia','forge-store.3dsx']}
(root/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
