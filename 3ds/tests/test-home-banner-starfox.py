#!/usr/bin/env python3
"""Test the installed candidate's sparse channel contract against actual Starfox."""
import argparse
from pathlib import Path
import struct
import sys
import unittest

R = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(R / '3ds/tools'))
import home_banner_validate as gate


def member(data):
    ptr = lambda p: p + struct.unpack_from('<i', data, p)[0]
    return ptr(ptr(data.find(b'CANM') + 28) + 40)


def sparse(data):
    report = gate.inspect(data)
    assert report['profile'] == 'fresh-starfox-sparse', 'constant scale/rotation channels reintroduced'
    return report


class StarfoxBanner(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.bnr = (args.bnr or args.candidate / 'banner.bnr').read_bytes()
        cls.model, cls.audio = gate.unpack_banner(cls.bnr)
        cls.reference = args.reference.read_bytes()

    def test_sparse_transform(self):
        r = sparse(self.model)
        self.assertEqual((r['translation_tracks'], r['rotation_tracks'], r['loop_frames']), (3, 0, 240))
        # The reserved W absence bit is the documented reader repair; all
        # six real scale/rotation channel flags/payloads must match Starfox.
        m, ref = member(self.model), member(self.reference)
        self.assertEqual(struct.unpack_from('<I', self.model, m)[0] & ~0x400000,
                         struct.unpack_from('<I', self.reference, ref)[0])
        self.assertEqual(self.model[m+20:m+48], self.reference[ref+20:ref+48])

    def test_curves_match_starfox_bytes(self):
        def curves(data):
            ptr = lambda p: p + struct.unpack_from('<i', data, p)[0]
            m = member(data)
            result = []
            for axis in range(3):
                c = ptr(m + 48 + axis*4)
                s = ptr(c + 20)
                count = struct.unpack_from('<I', data, s+12)[0]
                result.append((data[c:c+20], data[s:s+20+count*8]))
            return result
        self.assertEqual(curves(self.model), curves(self.reference))

    def test_reject_test18_constant_channels(self):
        b = bytearray(self.model)
        m = member(b)
        struct.pack_into('<I', b, m, 0x400fc0)
        struct.pack_into('<6f', b, m+20, 1., 1., 1., 0., 0., 0.)
        with self.assertRaises(AssertionError):
            sparse(b)

    def test_reject_absent_channel_payload(self):
        b = bytearray(self.model)
        struct.pack_into('<f', b, member(b)+20, 1.)
        with self.assertRaises(AssertionError):
            sparse(b)

    def test_reject_null_translation_curve(self):
        b = bytearray(self.model)
        struct.pack_into('<I', b, member(b)+48, 0)
        with self.assertRaises(AssertionError):
            sparse(b)

    def test_texture_budget_unchanged(self):
        self.assertEqual(sum(t['bytes'] for t in gate.textures(self.model)), 229376)

    def test_top_fade_leaves_lower_half_unchanged(self):
        def alpha(data):
            for p in range(0, len(data)-60, 4):
                if data[p:p+8] != b'\x11\x00\x00\x20TXOB' or struct.unpack_from('<I', data, p+52)[0] != 5:
                    continue
                image = p+56+struct.unpack_from('<i', data, p+56)[0]
                start = image+12+struct.unpack_from('<i', data, image+12)[0]
                rows = []
                for y in range(128):
                    sy = 127-y  # texture is vertically flipped before swizzling
                    row = []
                    for x in range(256):
                        morton = sum((((x >> b) & 1) << (2*b)) | (((sy >> b) & 1) << (2*b+1)) for b in range(3))
                        row.append(data[start + ((sy//8)*32 + x//8)*128 + morton*2])
                    rows.append(row)
                return rows
            raise AssertionError('missing LA8 background')
        baseline, _ = gate.unpack_banner((R/'3ds/build/banner-test/test-19/banner.bnr').read_bytes())
        actual, original = alpha(self.model), alpha(baseline)
        self.assertEqual(actual[64:], original[64:])
        self.assertTrue(all(value == 0 for row in actual[:12] for value in row))
        self.assertLess(actual[32][128], original[32][128])

    def test_audio_unchanged(self):
        self.assertEqual(gate.audio_info(self.audio)[0], dict(seconds=3, channels=2, rate=32000, loop=False))
        _, baseline = gate.unpack_banner((R/'3ds/build/banner-test/test-19/banner.bnr').read_bytes())
        self.assertEqual(self.audio, baseline)

    def test_final_cia(self):
        gate.verify_cia(args.cia or args.candidate / 'forge-banner-test-20.cia', self.bnr)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', required=True, type=Path)
    parser.add_argument('--candidate', type=Path, default=R/'3ds/build/banner-test/test-20')
    parser.add_argument('--cia', type=Path, help='Verify a full store CIA instead of the tiny test')
    parser.add_argument('--bnr', type=Path, help='BNR packaged by the selected build')
    args = parser.parse_args()
    unittest.main(argv=[sys.argv[0]])
