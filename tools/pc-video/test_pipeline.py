"""Stdlib pipeline contracts; no CUDA or GPU dependency."""
from fractions import Fraction
import importlib.util
import io
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch

HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("t6video_pipeline_tested", HERE / "t6video.py")
pipeline = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(pipeline)


class FakeProbe:
    def __init__(self, pts, result=0):
        self.stdout = io.StringIO("".join(f"{value}\n" for value in pts))
        self.result = result
        self.terminated = False

    def wait(self, timeout=None):
        return self.result

    def poll(self):
        return self.result

    def terminate(self):
        self.terminated = True

    def kill(self):
        self.terminated = True


class CadenceTests(unittest.TestCase):
    def check_pts(self, pts, rate=Fraction(30000, 1001), ticks="1/30000", result=0):
        proc = FakeProbe(pts, result)
        with tempfile.TemporaryDirectory(prefix="t6video-pts-test-") as name:
            with patch.object(pipeline.subprocess, "Popen", return_value=proc) as spawn:
                try:
                    return pipeline.check_cfr(Path("fixture.mov"), {"index": 2, "time_base": ticks},
                                              rate, "ffprobe-test", Path(name))
                finally:
                    self.assertTrue(proc.stdout.closed)
                    args = spawn.call_args.args[0]
                    self.assertEqual(args[args.index("-select_streams") + 1], "2")

    def test_ntsc_rates_stay_exact(self):
        self.assertEqual(self.check_pts([0, 1001, 2002, 3003]), 4)
        self.assertEqual(self.check_pts([0, 1001, 2002], Fraction(60000, 1001), "1/60000"), 3)

    def test_packet_decode_order_can_differ_from_presentation_order(self):
        self.assertEqual(self.check_pts([0, 3003, 1001, 2002]), 4)

    def test_demuxer_half_tick_quantization_is_accepted(self):
        rate = Fraction(30000, 1001)
        pts = [round(Fraction(index, 1) / rate * 1000) for index in range(12)]
        self.assertEqual(self.check_pts(pts, rate, "1/1000"), 12)

    def test_gap_duplicate_vfr_and_nonzero_start_are_refused(self):
        for pts in ([0, 1001, 3003], [0, 1001, 1001], [0, 1001, 2100],
                    [1001, 2002, 3003], [-1001, 0, 1001]):
            with self.subTest(pts=pts), self.assertRaises(ValueError):
                self.check_pts(pts)

    def test_missing_and_invalid_pts_are_refused(self):
        for pts in ([], [""], ["N/A"], ["not-an-integer"]):
            with self.subTest(pts=pts), self.assertRaises(ValueError):
                self.check_pts(pts)

    def test_failed_probe_is_not_accepted_as_a_complete_timeline(self):
        with self.assertRaises(ValueError):
            self.check_pts([0, 1001], result=1)

    @unittest.skipUnless(shutil.which("ffmpeg") and shutil.which("ffprobe"), "FFmpeg fixture tools unavailable")
    def test_real_cpu_ffmpeg_cfr_fixture(self):
        with tempfile.TemporaryDirectory(prefix="t6video-cfr-fixture-") as name:
            directory = Path(name)
            source = directory / "fixture.mkv"
            subprocess.run([shutil.which("ffmpeg"), "-nostdin", "-v", "error", "-xerror", "-n",
                            "-f", "lavfi", "-i", "testsrc2=size=160x128:rate=30000/1001",
                            "-frames:v", "5", "-c:v", "ffv1", str(source)],
                           check=True, timeout=20, capture_output=True)
            _, stream = pipeline.probe(source, shutil.which("ffprobe"))
            rate = Fraction(stream["avg_frame_rate"])
            self.assertEqual(rate, Fraction(30000, 1001))
            self.assertEqual(pipeline.check_cfr(source, stream, rate, shutil.which("ffprobe"), directory), 5)


class ShortReader(io.BytesIO):
    def __init__(self, data, limit):
        super().__init__(data)
        self.limit = limit

    def read(self, size=-1):
        return super().read(min(size, self.limit))


class ShortWriter:
    def __init__(self, limit, fail_after=None):
        self.limit = limit
        self.fail_after = fail_after
        self.data = bytearray()
        self.calls = 0

    def write(self, data):
        self.calls += 1
        if self.fail_after is not None and self.calls > self.fail_after:
            return 0
        size = min(len(data), self.limit)
        self.data.extend(data[:size])
        return size


class PipeContractTests(unittest.TestCase):
    def test_short_reads_form_whole_frames(self):
        source = ShortReader(b"0123456789abcdefghij", 3)
        self.assertEqual(pipeline.read_frame(source, 10), b"0123456789")
        self.assertEqual(pipeline.read_frame(source, 10), b"abcdefghij")
        self.assertIsNone(pipeline.read_frame(source, 10))

    def test_partial_final_frame_is_an_error(self):
        with self.assertRaises(ValueError):
            pipeline.read_frame(ShortReader(b"123456789", 2), 10)

    def test_immediate_eof_does_not_invent_a_frame(self):
        self.assertIsNone(pipeline.read_frame(io.BytesIO(), 16))

    def test_short_writes_preserve_every_byte_in_order(self):
        expected = bytes(range(251)) * 7
        target = ShortWriter(13)
        pipeline.write_all(target, expected)
        self.assertEqual(target.data, expected)
        self.assertGreater(target.calls, 1)

    def test_encoder_stops_accepting_data_is_an_error(self):
        target = ShortWriter(3, fail_after=1)
        with self.assertRaises(OSError):
            pipeline.write_all(target, b"123456789")
        self.assertEqual(target.data, b"123")

    def test_broken_pipe_propagates(self):
        class BrokenWriter:
            def write(self, data):
                raise BrokenPipeError("encoder exited")

        with self.assertRaises(BrokenPipeError):
            pipeline.write_all(BrokenWriter(), b"data")


class PublicationTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="t6video-publish-test-")
        self.directory = Path(self.temporary.name)
        self.source = self.directory / "completed.tmp"
        self.source.write_bytes(b"complete verified video")
        self.output = self.directory / "result.mp4"

    def tearDown(self):
        self.temporary.cleanup()

    def test_publish_is_same_file_and_survives_staged_name_cleanup(self):
        pipeline.publish(self.source, self.output)
        self.assertTrue(os.path.samefile(self.source, self.output))
        self.source.unlink()
        self.assertEqual(self.output.read_bytes(), b"complete verified video")

    def test_racing_existing_output_is_preserved(self):
        self.assertFalse(os.path.lexists(self.output))
        self.output.write_bytes(b"foreign file created after preflight")
        with self.assertRaises(OSError):
            pipeline.publish(self.source, self.output)
        self.assertEqual(self.output.read_bytes(), b"foreign file created after preflight")
        self.assertEqual(self.source.read_bytes(), b"complete verified video")

    def test_output_is_input_or_existing_hardlink_is_preserved(self):
        with self.assertRaises(OSError):
            pipeline.publish(self.source, self.source)
        os.link(self.source, self.output)
        with self.assertRaises(OSError):
            pipeline.publish(self.source, self.output)
        self.assertEqual(self.source.read_bytes(), b"complete verified video")
        self.assertTrue(os.path.samefile(self.source, self.output))

    def test_existing_dangling_symlink_is_preserved(self):
        try:
            os.symlink(self.directory / "missing-target", self.output)
        except (OSError, NotImplementedError) as error:
            self.skipTest(f"Symlink creation unavailable: {error}")
        with self.assertRaises(OSError):
            pipeline.publish(self.source, self.output)
        self.assertTrue(self.output.is_symlink())
        self.assertFalse(self.output.exists())

    def test_missing_source_does_not_create_output(self):
        with self.assertRaises(OSError):
            pipeline.publish(self.directory / "missing.tmp", self.output)
        self.assertFalse(os.path.lexists(self.output))


if __name__ == "__main__":
    unittest.main()
