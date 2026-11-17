#!/usr/bin/env python3
"""Fresh banner production gate, package integrity and observed failure regressions."""
import os,struct,sys,unittest,wave
from pathlib import Path
R=Path(__file__).resolve().parents[2];sys.path.insert(0,str(R/'3ds/tools'));import home_banner_validate as gate
class FreshBanner(unittest.TestCase):
 @classmethod
 def setUpClass(c):
  c.bnr=Path(os.environ.get('FORGE_TEST_BNR',str(R/'3ds/build/banner.bnr'))).read_bytes();c.model,c.audio=gate.unpack_banner(c.bnr)
 def test_scene(self):
  r=gate.inspect(self.model);expected=os.environ.get('FORGE_EXPECT_BANNER_PROFILE','fresh-static-front');self.assertEqual(r['profile'],expected);self.assertEqual(r['rotation_tracks'],0);self.assertEqual(r['translation_tracks'],0 if expected=='fresh-static-front' else 3)
 def test_textures(self):self.assertEqual(sum(t['bytes'] for t in gate.textures(self.model)),229376)
 def test_audio(self):self.assertEqual(gate.audio_info(self.audio)[0],dict(seconds=3,channels=2,rate=32000,loop=False))
 def test_cia(self):gate.verify_cia(Path(os.environ.get('FORGE_TEST_CIA',str(R/'3ds/build/forge-store.cia'))),self.bnr)
 def test_reject_old_table(self):
  b=bytearray(self.model);struct.pack_into('<II',b,148,0x54434944,44)
  with self.assertRaises(AssertionError):gate.inspect(b)
 def test_animation_resources(self):
  resources=gate.resource_table(self.model)
  if os.environ.get('FORGE_EXPECT_BANNER_PROFILE','fresh-static-front')=='fresh-static-front':
   self.assertFalse(any(resources[9:]));self.assertNotIn(b'CANM',self.model)
  else:self.assertEqual(resources[9:], [1,0,0,0,0,0,0])
 def test_recovery_policy(self):self.assertTrue(gate.require_static(self.model))
 def test_recovery_rejects_animation_slot(self):
  b=bytearray(self.model);field=28+9*8;target=32+struct.unpack_from('<i',b,32)[0]
  struct.pack_into('<Ii',b,field,1,target-field-4)
  with self.assertRaisesRegex(AssertionError,'Animated HOME banners'):gate.require_static(b)
 def test_reject_unfaced_node(self):
  b=bytearray(self.model)
  for rows in gate.dictionaries(b):
   for _,_,_,n,p in rows:
    if n==b'ForgeMedallion':struct.pack_into('<I',b,p+212,0)
  with self.assertRaises(AssertionError):gate.inspect(b)
if __name__=='__main__':unittest.main()
