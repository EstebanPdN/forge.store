#!/usr/bin/env python3
"""Build a separate, empty application containing the current HOME banner."""
from pathlib import Path
import os,subprocess,json,hashlib
R=Path(__file__).resolve().parents[2];D=R/'3ds';O=D/'build/banner-test';O.mkdir(parents=True,exist_ok=True)
sdk=Path(os.environ.get('DEVKITPRO',str(Path.home()/'devkitpro')));cc=sdk/'devkitARM/bin/arm-none-eabi-gcc';T=sdk/'tools/bin'
def run(*a):subprocess.run([str(x) for x in a],check=True,cwd=O)
run(cc,'-march=armv6k','-mtune=mpcore','-mfloat-abi=hard','-mtp=soft','-Os','-D__3DS__','-I'+str(sdk/'libctru/include'),D/'banner-test/main.c','-specs=3dsx.specs','-L'+str(sdk/'libctru/lib'),'-lctru','-lm','-o',O/'banner-test.elf')
rsf=(D/'forge-store.rsf').read_text().replace('Title                   : forge.store','Title                   : ForgeBannerTest').replace('CTR-H-FRGS','CTR-H-FRBT').replace('0xF0F5E','0xF0F5F').replace('RomFs:\n  RootPath                : "build/romfs"\n','')
(O/'test.rsf').write_text(rsf)
run(T/'bannertool','makebanner','-ci',D/'meta/banner.cgfx','-a',D/'meta/banner.wav','-o',O/'banner.bnr')
run(T/'bannertool','makesmdh','-s','Forge Banner Test','-l','Banner test - START exits','-p',"Esteban's Forge",'-i',D/'meta/icon.png','-o',O/'icon.icn')
run(T/'makerom','-f','cia','-o',O/'forge-banner-test.cia','-elf',O/'banner-test.elf','-rsf',O/'test.rsf','-icon',O/'icon.icn','-banner',O/'banner.bnr','-target','t')
run('python3',D/'tools/home_banner_validate.py',O/'banner.bnr','--cia',O/'forge-banner-test.cia')
p=O/'forge-banner-test.cia';report={'title_id':'000400000F0F5F00','bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'banner_sha256':hashlib.sha256((D/'meta/banner.cgfx').read_bytes()).hexdigest(),'hardware_verified':False};(O/'manifest.json').write_text(json.dumps(report,indent=2)+'\n');print(report)
