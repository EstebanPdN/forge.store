#!/usr/bin/env python3
"""Checks for the isolated continuous rigid Minish-style coin test."""
import importlib.util,struct,unittest,os
from pathlib import Path
R=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('gate',R/'3ds/tools/home_banner_validate.py');gate=importlib.util.module_from_spec(spec);spec.loader.exec_module(gate)
class Turn(unittest.TestCase):
 @classmethod
 def setUpClass(cls):cls.data=(R/os.environ.get('FORGE_TEST_MODEL','3ds/build/home-banner/minish-27/banner.cgfx')).read_bytes()
 def test_one_continuous_rigid_turn(self):
  r=gate.inspect(self.data);self.assertEqual((r['meshes'],r['triangles'],r['rotation_tracks'],r['loop_frames']),(7,324,1,270))
 def test_original_single_logo_texture(self):
  textures=gate.textures(self.data);self.assertEqual(sum(t['bytes'] for t in textures),224*1024);self.assertIn({'width':128,'height':128,'format':3,'bytes':32768},textures)
 def test_rejects_mismatched_hierarchy_order(self):
  b=bytearray(self.data);rows=next(rows for rows in gate.dictionaries(b) if any(r[3]==b'Scene root' and struct.unpack_from('<I',b,r[4])[0]!=0x40000000 for r in rows));nodes={r[3]:r[4] for r in rows[1:]};p=nodes[b'COMMON']+20;struct.pack_into('<i',b,p,nodes[b'ForgeBackdrop']-p)
  with self.assertRaisesRegex(AssertionError,'hierarchy links'):gate.inspect(b)
 def test_rejects_off_center_pivot(self):
  b=bytearray(self.data);p=next(r[4] for rows in gate.dictionaries(b) for r in rows[1:] if r[3]==b'ForgeTurnCoin' and struct.unpack_from('<I',b,r[4])[0]!=0x40000000 and b[r[4]+struct.unpack_from('<i',b,r[4])[0]:].startswith(b'ForgeTurnCoin\0'))
  struct.pack_into('<f',b,p+56,1.)
  with self.assertRaisesRegex(AssertionError,'central vertical axis'):gate.inspect(b)
 def test_rejects_wrong_turn_endpoint(self):
  b=bytearray(self.data);ptr=lambda p:p+struct.unpack_from('<i',b,p)[0];a=b.find(b'CANM');m=ptr(ptr(a+28)+40);seg=ptr(ptr(m+36)+20);struct.pack_into('<f',b,seg+32,1.)
  with self.assertRaises(AssertionError):gate.inspect(b)
 def test_production_keeps_static_recovery(self):
  with self.assertRaises(AssertionError):gate.require_static(self.data)
 def test_final_cia_icon_and_embedded_resource(self):
  p=R/'3ds/build/banner-test';banner=(p/'banner.bnr').read_bytes();self.assertEqual(gate.unpack_banner(banner)[0],self.data);self.assertEqual(gate.verify_cia(p/'forge-banner-test.cia',banner)['icon_flags'],0x125)
if __name__=='__main__':unittest.main()
