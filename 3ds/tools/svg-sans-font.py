#!/usr/bin/env python3
"""Bake an OFL Inter Latin subset for remote SVG sans-serif labels, offline."""
from pathlib import Path
from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont
from fontTools.subset import Subsetter,Options
import io,hashlib,json
root=Path(__file__).resolve().parents[2]
source=root/'assets/fonts/inter/Inter-var.ttf'
font=TTFont(source)
font=instantiateVariableFont(font,{axis.axisTag:400 if axis.axisTag=='wght' else axis.defaultValue for axis in font['fvar'].axes},inplace=True)
sub=Subsetter(options=Options());sub.populate(unicodes=list(range(32,256)));sub.subset(font)
out=io.BytesIO();font.save(out);data=out.getvalue()
target=root/'3ds/source/vendor/remote_svg_sans_font.inc'
target.write_text('// Inter, SIL Open Font License; assets/fonts/inter/OFL.txt.\nstatic const unsigned char remote_svg_sans_font[]={\n'+',\n'.join(','.join(str(b) for b in data[i:i+24]) for i in range(0,len(data),24))+'\n};\n')
(root/'3ds/build/gen/svg-sans-font.json').write_text(json.dumps({'source':str(source.relative_to(root)),'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'subset_bytes':len(data),'subset_sha256':hashlib.sha256(data).hexdigest()},indent=2)+'\n')
print('Inter SVG subset:',len(data),'bytes')
