from flask import Flask, request, send_from_directory
from werkzeug.utils import secure_filename
import os
from pathlib import Path

BASE = Path(__file__).resolve().parent
CAPTURES = BASE / "captures"
CAPTURES.mkdir(exist_ok=True)
TRANSCRIPTS = BASE / "transcripts"
TRANSCRIPTS.mkdir(exist_ok=True)

app = Flask(__name__)
# Max upload size is 32 MB
app.config['MAX_CONTENT_LENGTH'] = 32 * 1000 * 1000


@app.route("/transcripts", methods=["GET"])
def transcripts():
    return [f.name for f in TRANSCRIPTS.glob("*.txt")]


@app.route("/transcripts/<string:name>", methods=["GET"])
def transcript(name):
    return send_from_directory(TRANSCRIPTS, name)


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

    # TODO: whisper transcription
    (TRANSCRIPTS / f"{capture}.txt").write_text(f"Transcription for {capture} (size: {size} bytes)")
    return {"stored": final_name.name, "size": size}, 200
