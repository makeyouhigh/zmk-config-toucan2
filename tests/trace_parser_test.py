import copy
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location(
    "trace_parser", Path(__file__).parents[1] / "tools/parse_force_trace.py")
parser = importlib.util.module_from_spec(spec)
spec.loader.exec_module(parser)


def fixture(start=100, sample=116):
    raw = bytearray(16)
    raw[4] = 1
    raw[5:7] = (-12).to_bytes(2, "big", signed=True)
    raw[13:15] = (1234).to_bytes(2, "big")
    fields = [sample, 1000, 700, bytes(raw)] + [0] * 21
    fields[4 + 1] = 1 << 3
    fields[4 + 13] = 1
    payload = parser.WIRE.pack(*fields)
    return {"lines": [
        f"DATA,v11,1,1,0,{start},68",
        f"R,0,{payload.hex()},{parser.checksum(payload):08x}", "END,1"]}


class TraceTests(unittest.TestCase):
    def test_v14_fixed_levels(self):
        raw = bytearray(16)
        raw[4] = 1
        raw[13:15] = (4700).to_bytes(2, 'big')
        payload = parser.WIRE_V2.pack(116, 900, 850, bytes(raw),
            1, 1 | (1 << 2), 4250, 4500, 4000, 0, 0,
            -3, 4, 1, 0, 32767, 32767, 0, 0, 1, 0)
        data = {'lines': ['DATA,v14,2,1,0,100,60',
            f'R,0,{payload.hex()},{parser.checksum(payload):08x}', 'END,1']}
        result = parser.parse(data)
        frame = result['frames'][0]
        self.assertEqual(frame['after_state'], ['active', 'down'])
        self.assertEqual((frame['lock_level'], frame['press_level'], frame['release_level']),
                         (4250, 4500, 4000))
        self.assertNotIn('baseline', frame)
        self.assertEqual(result['force_edges'][0]['press_level'], 4500)
        data['lines'][0] = 'DATA,v14,1,1,0,100,60'
        with self.assertRaises(ValueError): parser.parse(data)

    def test_v13_version(self):
        data = fixture()
        data["lines"][0] = data["lines"][0].replace("v11", "v13")
        self.assertEqual(parser.parse(data)["version"], "v13")

    def test_v12_version(self):
        data = fixture()
        data["lines"][0] = data["lines"][0].replace("v11", "v12")
        self.assertEqual(parser.parse(data)["version"], "v12")

    def test_decode_signed_sensor_and_flags(self):
        result = parser.parse(fixture())
        frame = result["frames"][0]
        self.assertEqual((frame["strength"], frame["raw_dx"]), (1234, -12))
        self.assertEqual(frame["after_state"], ["down"])
        self.assertEqual(result["force_edges"][0]["event"], 1)

    def test_uptime_wrap(self):
        result = parser.parse(fixture(0xFFFFFFF8, 8))
        self.assertEqual(result["frames"][0]["relative_ms"], 16)

    def test_reject_corruption_missing_end_index_and_version(self):
        original = fixture()
        mutations = [
            lambda d: d["lines"].pop(),
            lambda d: d["lines"].__setitem__(0, d["lines"][0].replace("v11", "v10")),
            lambda d: d["lines"].__setitem__(1, d["lines"][1].replace("R,0,", "R,1,")),
            lambda d: d["lines"].__setitem__(1, d["lines"][1][:-1] +
                                            ("0" if d["lines"][1][-1] != "0" else "1")),
        ]
        for mutate in mutations:
            data = copy.deepcopy(original)
            mutate(data)
            with self.assertRaises(ValueError):
                parser.parse(data)

    def test_full_capture_is_flagged(self):
        data = fixture()
        data["lines"][0] = data["lines"][0].replace(",1,0,100,", ",1,1,100,")
        self.assertTrue(parser.parse(data)["buffer_full"])


if __name__ == "__main__":
    unittest.main()
