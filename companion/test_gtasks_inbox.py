import json
import tempfile
import unittest
from pathlib import Path

from gtasks_inbox import send_task


class SendTaskTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.app_dir = Path(self._tmp.name)

    def tearDown(self):
        self._tmp.cleanup()

    def test_writes_inbox_file_and_wakes_service(self):
        (self.app_dir / "inbox").mkdir()
        (self.app_dir / "queue").mkdir()

        self.assertTrue(send_task("note_0007", "buy milk", self.app_dir))

        files = list((self.app_dir / "inbox").iterdir())
        self.assertEqual([f.name for f in files], ["note_0007.json"])
        self.assertEqual(
            json.loads(files[0].read_text()), {"text": "buy milk", "capture": "note_0007"}
        )
        self.assertTrue((self.app_dir / "queue" / "sync-now").exists())

    def test_off_when_gtasks_sync_not_set_up(self):
        self.assertFalse(send_task("note_0007", "buy milk", self.app_dir))
        self.assertEqual(list(self.app_dir.iterdir()), [])


if __name__ == "__main__":
    unittest.main()
