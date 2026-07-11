from pathlib import Path
from queue import Queue
from threading import Thread
import hmac
import os

from faster_whisper import WhisperModel
from flask import Flask, request, send_from_directory
from werkzeug.utils import secure_filename

BASE = Path(__file__).resolve().parent
CAPTURES = BASE / "captures"
CAPTURES.mkdir(exist_ok=True)
TRANSCRIPTS = BASE / "transcripts"
TRANSCRIPTS.mkdir(exist_ok=True)

app = Flask(__name__)
# Max upload size is 32 MB
app.config['MAX_CONTENT_LENGTH'] = 32 * 1000 * 1000

# Shared bearer token, hardcoded on both sides (device in wifi_secrets.h).
# Fail closed: no default -- refuse to start rather than run with a public secret.
_token = os.environ.get("COMPANION_TOKEN")
if not _token:
    raise RuntimeError(
        "COMPANION_TOKEN env var must be set (shared bearer secret, no default)"
    )
COMPANION_TOKEN: str = _token

MODEL = WhisperModel("base.en", device="cpu", compute_type="int8")
JOBS = Queue()


@app.before_request
def require_bearer_token():
    scheme, _, token = request.headers.get("Authorization", "").partition(" ")
    if scheme != "Bearer" or not hmac.compare_digest(token, COMPANION_TOKEN):
        return "", 401  # contract: empty body


@app.route("/transcripts", methods=["GET"])
def transcripts():
    return [f.name for f in TRANSCRIPTS.glob("*.txt")]


@app.route("/transcripts/<string:name>", methods=["GET"])
def transcript(name):
    return send_from_directory(TRANSCRIPTS, name)


def transcribe_worker():
    while True:
        capture = JOBS.get()
        try:
            audio_path = CAPTURES / f"{capture}.wav"
            final = TRANSCRIPTS / f"{capture}.txt"
            tmp = final.with_name(final.name + ".part")
            segments, _info = MODEL.transcribe(str(audio_path))
            # Consuming the generator is what runs inference (it's lazy).
            text = "".join(segment.text for segment in segments).strip()
            tmp.write_text(text + "\n")
            tmp.replace(final)  # atomic: GET /transcripts lists it only when complete
        except Exception as e:
            print(f"Error transcribing {capture}: {e}")
        finally:
            JOBS.task_done()


def enqueue_pending():
    # State-driven recovery: any committed WAV without a .txt is pending.
    # Runs at startup so crashes / failed jobs get retried idempotently.
    for wav in CAPTURES.glob("*.wav"):
        if not (TRANSCRIPTS / f"{wav.stem}.txt").exists():
            JOBS.put(wav.stem)


Thread(target=transcribe_worker, daemon=True).start()
enqueue_pending()


@app.route("/captures/<string:capture>.wav", methods=["POST"])
def upload_capture(capture):
    capture = secure_filename(capture)
    final_name = CAPTURES / f"{capture}.wav"
    tmp_name = CAPTURES / f"{capture}.wav.part"
    size = 0

    try:
        with tmp_name.open("wb") as file:
            while chunk := request.stream.read(8192):
                file.write(chunk)
                size += len(chunk)
            file.flush()
            os.fsync(file.fileno())
        tmp_name.replace(final_name)
    except Exception:
        # Dropped upload / write error: never leave a stray .part or commit a
        # partial file. Device leaves it unsynced and retries next sync.
        tmp_name.unlink(missing_ok=True)
        return {"error": "upload failed"}, 500

    JOBS.put(capture)
    return {"stored": final_name.name, "size": size}, 200
