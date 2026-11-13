#!/usr/bin/env python3
"""Exercise the same serialized-resource gate used by build.sh, including corruptions."""
import importlib.util, os, struct, unittest, wave
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('gate',ROOT/'3ds/tools/home_banner_validate.py');gate=importlib.util.module_from_spec(spec);spec.loader.exec_module(gate)
class BannerTests(unittest.TestCase):
    @classmethod
    def setUpClass(c):
        c.bnr=(Path(os.environ.get('FORGE_TEST_BNR',str(ROOT/'3ds/build/banner.bnr')))).read_bytes()
        c.model,c.audio=gate.unpack_banner(c.bnr)
    def test_packaged_resource(self):
        r=gate.inspect(self.model);self.assertEqual(r['meshes'],6)
        self.assertEqual(r['skeletal_animations'][0]['frames'],312)
        self.assertEqual(sum(t['bytes'] for t in gate.textures(self.model)),229376)
        self.assertEqual(gate.audio_info(self.audio)[0]['seconds'],3)
        self.assertIn((512,128),[(t['width'],t['height']) for t in gate.textures(self.model)])
    def test_pcm_roundtrip(self):
        with wave.open(str(ROOT/'3ds/meta/banner.wav')) as w:frames=w.readframes(w.getnframes())
        _,channels=gate.audio_info(self.audio)
        for i in range(2):self.assertEqual(channels[i],b''.join(frames[j:j+2] for j in range(i*2,len(frames),4)))
    def test_truncation_and_oversize(self):
        for bad in (self.model[:-1],self.model+b'\0'*524288):
            with self.assertRaises(AssertionError):gate.inspect(bad)
    def test_unresolved_bone(self):
        b=bytearray(self.model);p=b.find(b'\0\0\0\x01SOBJ');struct.pack_into('<i',b,p+112,0)
        with self.assertRaises(AssertionError):gate.inspect(b)
    def test_bad_dictionary(self):
        b=bytearray(self.model);p=b.find(b'DICT');struct.pack_into('<H',b,p+16,65535)
        with self.assertRaises(AssertionError):gate.inspect(b)
    def test_forbidden_texture(self):
        b=bytearray(self.model);p=b.find(b'\x11\0\0\x20TXOB');struct.pack_into('<I',b,p+52,0)
        with self.assertRaises(AssertionError):gate.textures(b)
    def test_audio_limits(self):
        for offset,value in ((0x4c,48000),(0x54,96001),(0x5c,1)):
            b=bytearray(self.audio);struct.pack_into('<I',b,offset,value)
            with self.assertRaises(AssertionError):gate.audio_info(b)
    def test_lateral_rotation(self):
        r=gate.inspect(self.model)
        self.assertTrue(r['medallion_billboard_off'])
        self.assertEqual(r['skeletal_animations'][0]['rotation_tracks'],1)

    def test_reject_tilt_and_moving_pivot(self):
        for slot,value in ((3,.1),(6,-7.0),(7,1.1)):
            b=bytearray(self.model)
            pointer=lambda p:p+struct.unpack_from('<i',b,p)[0]
            anim=b.find(b'CANM');table=pointer(anim+28);member=pointer(table+40)
            field=member+20+4*slot+(4 if slot>=6 else 0)
            struct.pack_into('<f',b,field,value)
            with self.assertRaises(AssertionError):gate.inspect(b)

    def test_reject_implicit_trs(self):
        b=bytearray(self.model)
        pointer=lambda p:p+struct.unpack_from('<i',b,p)[0]
        anim=b.find(b'CANM');table=pointer(anim+28);member=pointer(table+40)
        flags=struct.unpack_from('<I',b,member)[0]
        struct.pack_into('<I',b,member,(flags & ~0x2000) | 0x800000)
        struct.pack_into('<I',b,member+48,0)
        with self.assertRaises(AssertionError):gate.inspect(b)

    def test_reject_missing_rotation_w_flag(self):
        # Recreate the exact byte-level defect in rejected hardware test 14.
        b=bytearray(self.model)
        pointer=lambda p:p+struct.unpack_from('<i',b,p)[0]
        anim=b.find(b'CANM');table=pointer(anim+28);member=pointer(table+40)
        flags=struct.unpack_from('<I',b,member)[0]
        self.assertTrue(flags & 0x400000)
        self.assertEqual(struct.unpack_from('<I',b,member+44)[0],0)
        struct.pack_into('<I',b,member,flags & ~0x400000)
        with self.assertRaisesRegex(AssertionError,'Rotation W'):gate.inspect(b)

    def test_reject_null_present_curve(self):
        b=bytearray(self.model)
        pointer=lambda p:p+struct.unpack_from('<i',b,p)[0]
        anim=b.find(b'CANM');table=pointer(anim+28);member=pointer(table+40)
        struct.pack_into('<I',b,member+36,0)  # nonconstant Y rotation curve
        with self.assertRaisesRegex(AssertionError,'null animation'):gate.inspect(b)

    def test_extended_banner_flag(self):
        icon=bytearray(0x36c0);icon[:4]=b'SMDH'
        struct.pack_into('<I',icon,0x2028,0x105)
        with self.assertRaisesRegex(AssertionError,'extendedbanner'):gate.verify_icon(icon)
        struct.pack_into('<I',icon,0x2028,0x125)
        self.assertEqual(gate.verify_icon(icon),0x125)

    def test_oval_alpha_precision(self):
        b=self.model
        for p in range(0,len(b)-60,4):
            if b[p:p+8]==b'\x11\0\0\x20TXOB' and struct.unpack_from('<I',b,p+52)[0]==5:
                image=p+56+struct.unpack_from('<i',b,p+56)[0]
                size,rel=struct.unpack_from('<Ii',b,image+8);start=image+12+rel
                alpha=b[start:start+size:2]
                self.assertGreater(len(set(alpha)),128)
                self.assertEqual((min(alpha),max(alpha)),(0,255));return
        self.fail('LA8 oval missing')
    def test_cia_matches(self):
        gate.verify_cia(Path(os.environ.get('FORGE_TEST_CIA',str(ROOT/'3ds/build/forge-store.cia'))),self.bnr)
if __name__=='__main__':unittest.main()
