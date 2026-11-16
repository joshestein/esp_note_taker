import json
import tempfile
import unittest
from pathlib import Path

from gtasks_inbox import is_set_up, send_task


class SendTaskTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.app_dir = Path(self._tmp.name) / "gtasks-sync"

    def tearDown(self):
        self._tmp.cleanup()

    def make_service_dirs(self):
        (self.app_dir / "inbox").mkdir(parents=True)
        (self.app_dir / "queue").mkdir()

    def test_not_set_up(self):
        self.assertFalse(is_set_up(self.app_dir))
        self.assertFalse(send_task("note_0001", "buy milk", self.app_dir))
        self.assertFalse(self.app_dir.exists())

    def test_data_dir_without_inbox_is_not_set_up(self):
        # An older service that cannot read the inbox. The Companion makes no dirs.
        self.app_dir.mkdir()
        self.assertFalse(send_task("note_0001", "buy milk", self.app_dir))
        self.assertEqual(list(self.app_dir.iterdir()), [])

    def test_writes_task_and_asks_for_sync(self):
        self.make_service_dirs()
        self.assertTrue(send_task("note_0001", "buy milk", self.app_dir))
        self.assertEqual(
            json.loads((self.app_dir / "inbox" / "note_0001.json").read_text()),
            {"text": "buy milk", "capture": "note_0001"},
        )
        self.assertTrue((self.app_dir / "queue" / "sync-now").exists())
        self.assertEqual([p.name for p in (self.app_dir / "inbox").iterdir()], ["note_0001.json"])

    def test_same_capture_overwrites(self):
        self.make_service_dirs()
        send_task("note_0001", "buy milk", self.app_dir)
        send_task("note_0001", "buy eggs", self.app_dir)
        files = list((self.app_dir / "inbox").iterdir())
        self.assertEqual(len(files), 1)
        self.assertEqual(json.loads(files[0].read_text())["text"], "buy eggs")


if __name__ == "__main__":
    unittest.main()
