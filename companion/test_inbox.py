import json
import tempfile
import unittest
from pathlib import Path

from inbox import is_set_up, send

SERVICE = "keep-sync"


class SendTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.base = Path(self._tmp.name)
        self.app_dir = self.base / SERVICE

    def tearDown(self):
        self._tmp.cleanup()

    def make_service_dirs(self):
        (self.app_dir / "inbox").mkdir(parents=True)
        (self.app_dir / "queue").mkdir()

    def inbox_files(self):
        return sorted(p.name for p in (self.app_dir / "inbox").iterdir())

    def test_not_set_up(self):
        self.assertFalse(is_set_up(SERVICE, self.base))
        self.assertFalse(send(SERVICE, "note_0001", {"text": "buy milk"}, self.base))
        self.assertFalse(self.app_dir.exists())

    def test_data_dir_without_inbox_is_not_set_up(self):
        # An older service that cannot read the inbox. The Companion makes no dirs.
        self.app_dir.mkdir()
        self.assertFalse(send(SERVICE, "note_0001", {"text": "buy milk"}, self.base))
        self.assertEqual(list(self.app_dir.iterdir()), [])

    def test_writes_fields_and_asks_for_sync(self):
        self.make_service_dirs()
        fields = {"text": "buy milk", "append_to": "abc.0123456789abcdef"}
        self.assertTrue(send(SERVICE, "note_0001", fields, self.base))
        self.assertEqual(
            json.loads((self.app_dir / "inbox" / "note_0001.json").read_text()),
            {**fields, "capture": "note_0001"},
        )
        self.assertTrue((self.app_dir / "queue" / "sync-now").exists())
        self.assertEqual(self.inbox_files(), ["note_0001.json"])

    def test_same_capture_overwrites(self):
        self.make_service_dirs()
        send(SERVICE, "note_0001", {"text": "buy milk"}, self.base)
        send(SERVICE, "note_0001", {"text": "buy eggs"}, self.base)
        self.assertEqual(self.inbox_files(), ["note_0001.json"])
        data = json.loads((self.app_dir / "inbox" / "note_0001.json").read_text())
        self.assertEqual(data["text"], "buy eggs")


if __name__ == "__main__":
    unittest.main()
