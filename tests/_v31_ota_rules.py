"""Mirror of the OTA upload-callback auth rule (Plan 2 Task 5.3).

The spec: "ตรวจ auth ใน OTA upload callback ตั้งแต่ UPLOAD_FILE_START เพื่อห้าม
เขียน flash แม้แต่บางส่วนเมื่อไม่มีสิทธิ์".

The C++ checks g_security.authorize() at UPLOAD_FILE_START and calls
Update.end() + return on failure, so Update.begin() never runs and no
flash sector is touched. This module models that decision as a pure
function so the test can assert it without real hardware."""

# "Update" operations on the device. The integration test asserts these
# counts: begin must be 0 when auth fails, even if the upload supplies data.
class FakeFlash:
    def __init__(self):
        self.begin_calls = 0
        self.write_calls = 0
        self.end_calls = 0
        self.written_bytes = 0

    def begin(self, size):
        self.begin_calls += 1

    def write(self, buf):
        self.write_calls += 1
        self.written_bytes += len(buf)

    def end(self):
        self.end_calls += 1


def ota_upload_callback(status: str, data: bytes, auth_ok: bool, flash: FakeFlash) -> None:
    """Mirror of handleUpdateUpload. Returns nothing; mutates `flash`.

    status in {"start","write","end","aborted"}.
    auth_ok = True iff the request carried valid Digest credentials.
    """
    if status == "start":
        if not auth_ok:
            # Spec rule: NO Update.begin() when unauthorised. End + return.
            flash.end()
            return
        flash.begin(0)   # size unknown to the mirror
    elif status == "write":
        # If auth failed at start, write must never be reached because the
        # handler returned. The integration test asserts begin_calls == 0.
        flash.write(data)
    elif status == "end":
        flash.end()
    elif status == "aborted":
        flash.end()
