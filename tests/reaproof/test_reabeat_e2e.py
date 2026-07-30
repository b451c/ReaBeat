"""ReaProof E2E specs for ReaBeat v2.0.3 fixes.

Drives a REAL REAPER (isolated profile) with the built extension installed,
clicks the actual UI via OS-level input synthesis, and asserts on state read
back through REAPER's own API (never on ReaBeat's own claims).

Covers (see reaproof_features.json):
  F1 window toggle lifecycle        (_ReaBeat_ShowWindow)
  F2 keyboard reaches REAPER while the ReaBeat window is closed (pump gating)
  F3 stretch-marker srcpos convention on a TRIMMED item (v2.0.3 C1 fix)
  F4 tempo map constant-mode marker lands inside the cleared range
  F5 detection GUID guard (result not applied to a different item)

Run:  see README.md in this directory.

Doctrine notes:
- One module-scoped session; tests are ORDERED phases of one workflow
  (REAPER launches are ~15 s; detection ~30-60 s). Re-run the whole module
  to reproduce any single test.
- Click targets are located from PIXELS at runtime (gold Detect button
  calibrates the frame offset; the gold Apply bar anchors the radio rows).
  Only the Detect center (429, 56) comes from MainComponent::resized()
  math @ UI scale 1.0 / 500x660 client - and it is verified by the
  calibration itself (the gold blob must sit within 8 pt of it).
- The srcpos oracle is mutation-verified in-test via reaproof.mutation.
"""
from __future__ import annotations

import os
import time
from pathlib import Path

import numpy as np
import pytest
import soundfile as sf

from reaproof.runner.session import ReaperSession
from reaproof.observe.input import _MacMouse
from reaproof.mutation import mutation_check

# ---------------------------------------------------------------------------
# Subject resolution
# ---------------------------------------------------------------------------
REPO = Path(__file__).resolve().parents[2]
DYLIB = Path(os.environ.get("REABEAT_DYLIB",
                            REPO / "build" / "reaper_reabeat-arm64.dylib"))
ORT = Path(os.environ.get("REABEAT_ORT",
                          REPO / "vendor" / "onnxruntime" / "lib"
                               / "libonnxruntime.1.24.4.dylib"))

pytestmark = pytest.mark.skipif(
    not (DYLIB.exists() and ORT.exists()),
    reason=f"subject not built: {DYLIB} / {ORT}")

# Source-file beat grid of the synthetic click track (seconds)
BPS = 0.5          # 120 BPM click every 0.5 s
CLIP_SEC = 40.0
# Slip-edit offset. 1.75 mod 0.5 = 0.25 = the maximum possible distance to
# the click grid, so the OLD bug (srcpos = beat + D_STARTOFFS) lands every
# marker as far from the grid as possible -> unambiguous red.
STARTOFFS = 1.75
# Detection tolerance: OnsetRefinement snaps to transients within +/-30 ms
# and the model itself is ~20 ms; 60 ms still cleanly separates a correct
# marker (<=60 ms from grid) from the bug (250 ms from grid).
TOL = 0.060

# ---------------------------------------------------------------------------
# Layout constants derived from MainComponent::resized() @ scale 1.0, 500x660
# client area (area = bounds reduced by (16,10) -> x:16..484, y:10..650).
# ---------------------------------------------------------------------------
# Detect button: setBounds(area.getRight()-110, y-2, 110, 24) with y=46+36..
#   -> rect (374, 44)-(484, 68), center:
DETECT = (429, 56)
# Controls BELOW the waveform are NOT addressed by derived absolute
# coordinates (that proved fragile) - the Apply bar is pixel-located at
# runtime (Ui.locate_apply) and radio rows are addressed relative to it.
# Neutral activation point (dead zone between header rows, no control)
NEUTRAL = (250, 74)

CLIENT_W, CLIENT_H = 500, 660

# Cross-test dependency flags (module runs as ordered phases)
STATE: dict = {}


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
def make_clicktrack(path: Path, *, bpm: float = 120.0, seconds: float = CLIP_SEC,
                    sr: int = 44100) -> None:
    """Sharp click every beat, accent every 4th. Pure-silence floor is fine:
    overall RMS ~0.05 >> BeatDetector's 0.001 silence gate."""
    n = int(seconds * sr)
    y = np.zeros(n, dtype=np.float32)
    step = 60.0 / bpm
    t = np.arange(int(0.004 * sr)) / sr
    burst = (np.sin(2 * np.pi * 3000 * t) * np.exp(-t * 1500)).astype(np.float32)
    k = 0
    pos = 0.0
    while pos < seconds - 0.01:
        i = int(pos * sr)
        amp = 0.95 if k % 4 == 0 else 0.65
        seg = burst[: max(0, min(len(burst), n - i))]
        y[i:i + len(seg)] += amp * seg
        k += 1
        pos += step
    sf.write(str(path), y, sr)


class Ui:
    """Client-coordinate clicks on the ReaBeat window (always-on-top).

    The client-origin offset inside the window frame is NOT computed from
    CGWindowBounds arithmetic (that mapping proved wrong by one UI row on
    at least one machine) - it is CALIBRATED from pixels: the gold
    "Detect Beats" button is located in a window screenshot and its known
    client position (429, 56) anchors every other coordinate.
    """

    def __init__(self, sess):
        self.sess = sess
        self.mouse = _MacMouse()
        self.oy = None      # client-y origin offset within the window frame

    def _front(self):
        import subprocess
        subprocess.run(["osascript", "-e",
                        'tell application "System Events" to set frontmost of '
                        f'(first process whose unix id is {self.sess.handle.pid}) to true'],
                       capture_output=True)

    def _calibrate(self):
        """Find the gold Detect button in a fresh window screenshot."""
        import subprocess, tempfile
        from PIL import Image
        import Quartz
        found = wait_for_reabeat_window(self.sess, front=self._front)
        if found is None:
            shot(self.sess, "calibration-window-missing")
            raise AssertionError(
                "DRIVER failure (not a subject verdict): ReaBeat window never "
                "came on screen within the timeout - the machine is likely in "
                "active use or the test instance sits on another Space. "
                "Evidence: calibration-window-missing*.png in the shots dir. "
                "Re-run on an idle desktop.")
        wid, x, y, w, h = found
        with tempfile.NamedTemporaryFile(suffix=".png", delete=False) as f:
            png = f.name
        subprocess.run(["screencapture", "-x", "-o", "-l", str(wid), png],
                       capture_output=True)
        img = Image.open(png).convert("RGB")
        scale = img.width / w          # Retina: 2.0, else 1.0
        # Gold filled button (0xC8A040-ish) in the right half of the header
        # region. The title text is also gold but thin and on the left.
        px = img.load()
        xs, ys = [], []
        for iy in range(0, min(img.height, int(120 * scale))):
            for ix in range(int(img.width * 0.55), img.width):
                r, g, b = px[ix, iy]
                if 170 <= r <= 235 and 130 <= g <= 185 and 30 <= b <= 100:
                    xs.append(ix)
                    ys.append(iy)
        assert len(xs) > 200 * scale * scale, (
            f"calibration failed: gold Detect button not found ({len(xs)} px)")
        cx_img = (min(xs) + max(xs)) / 2 / scale   # window-frame points
        cy_img = (min(ys) + max(ys)) / 2 / scale
        # Known client position of the Detect center anchors the offset.
        self.oy = cy_img - DETECT[1]
        ox = cx_img - DETECT[0]
        assert abs(ox) < 8, f"unexpected horizontal offset {ox}"
        assert -5 <= self.oy <= 60, f"implausible client-origin offset {self.oy}"

    def _client_to_screen(self, cx: float, cy: float):
        self._front()
        if self.oy is None:
            self._calibrate()
        found = wait_for_reabeat_window(self.sess, front=self._front, timeout=6.0)
        assert found is not None, (
            "DRIVER failure: ReaBeat window left the screen mid-test")
        _, x, y, w, h = found
        return x + cx, y + self.oy + cy

    def click(self, pt, *, activate_first: bool = True):
        # A CGEvent click on a non-key window can be swallowed as the
        # activation click -> optionally spend one click on a neutral spot.
        if activate_first:
            nx, ny = self._client_to_screen(*NEUTRAL)
            self.mouse.click(nx, ny)
            time.sleep(0.25)
        x, y = self._client_to_screen(*pt)
        self.mouse.click(x, y)
        time.sleep(0.25)

    # -- pixel location of the Apply bar (frame coordinates) ----------------
    # Derived layout constants proved fragile below the waveform; the Apply
    # button is unambiguous instead: a full-width (468 pt) gold bar in the
    # lower half. Radio rows are then addressed RELATIVE to its top edge
    # (spacings straight from resized(): each radio row is 28 pt, the gap
    # above Apply is 8 pt, radio text centre sits 11 pt into its row).
    def locate_apply(self):
        """Return (center_x, center_y, top_y) of the Apply bar in frame
        points, or None while results (and thus Apply) are not shown."""
        import subprocess, tempfile
        from PIL import Image
        found = find_reabeat_window(self.sess)
        if not found:
            return None
        wid, x, y, w, h = found
        with tempfile.NamedTemporaryFile(suffix=".png", delete=False) as f:
            png = f.name
        subprocess.run(["screencapture", "-x", "-o", "-l", str(wid), png],
                       capture_output=True)
        img = Image.open(png).convert("RGB")
        scale = img.width / w
        px = img.load()
        rows = []
        for iy in range(int(img.height * 0.5), img.height):
            run = 0
            best = 0
            for ix in range(img.width):
                r, g, b = px[ix, iy]
                if 150 <= r <= 240 and 115 <= g <= 190 and 25 <= b <= 110:
                    run += 1
                    best = max(best, run)
                else:
                    run = 0
            if best >= 0.85 * img.width:   # full-width bar (strength slider is narrower)
                rows.append(iy)
        if not rows:
            return None
        top, bot = min(rows), max(rows)
        if (bot - top) / scale < 15:       # a 28 pt button, not a slider track
            return None
        return (img.width / 2 / scale, (top + bot) / 2 / scale, top / scale)

    def click_frame(self, fx: float, fy: float):
        found = wait_for_reabeat_window(self.sess, front=self._front, timeout=6.0)
        assert found is not None, (
            "DRIVER failure: ReaBeat window left the screen mid-test")
        _, x, y, w, h = found
        self._front()
        self.mouse.click(x + fx, y + fy)
        time.sleep(0.3)


SHOTS = Path(os.environ.get("REABEAT_TEST_SHOTS",
                            "/tmp/reabeat-e2e-shots"))


def find_reabeat_window(sess):
    """The plugin window, STRICTLY: exact title AND the designed 500 pt
    width. The session can own other windows whose names contain 'ReaBeat'
    (observed: a 481 pt one), and CGWindowList z-order decides which comes
    first - substring matching made clicks land one UI row off at random.
    Returns (window_id, x, y, w, h) or None."""
    import Quartz
    wins = Quartz.CGWindowListCopyWindowInfo(
        Quartz.kCGWindowListOptionOnScreenOnly
        | Quartz.kCGWindowListExcludeDesktopElements, Quartz.kCGNullWindowID)
    for w in wins:
        if (w.get("kCGWindowOwnerPID") == sess.handle.pid
                and (w.get("kCGWindowName") or "") == "ReaBeat"):
            b = w["kCGWindowBounds"]
            if abs(b["Width"] - CLIENT_W) <= 2:
                return (w["kCGWindowNumber"], b["X"], b["Y"],
                        b["Width"], b["Height"])
    return None


def wait_for_reabeat_window(sess, front=None, timeout: float = 12.0):
    """Poll for the plugin window instead of a single check. The lookup is
    CGWindowList OnScreenOnly, so it is z-order- and Space-dependent: on a
    busy machine (user active, window briefly occluded or on another Space)
    a one-shot check after a fixed sleep flakes even though the window
    exists and is healthy. Returns the window tuple or None on timeout."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        if front:
            front()
        found = find_reabeat_window(sess)
        if found:
            return found
        time.sleep(0.5)
    return None


def shot(sess, name: str):
    """Screenshot of the ReaBeat window (evidence artifact)."""
    import subprocess
    SHOTS.mkdir(parents=True, exist_ok=True)
    found = find_reabeat_window(sess)
    if found:
        subprocess.run(["screencapture", "-x", "-o", "-l", str(found[0]),
                        str(SHOTS / f"{name}.png")], capture_output=True)
    else:
        subprocess.run(["screencapture", "-x", str(SHOTS / f"{name}-full.png")],
                       capture_output=True)


def press_space():
    """Space as a REAL key event (kVK_Space=49). REAPER's transport shortcut
    does not fire on unicode-string CGEvents (type_text) - it needs the
    virtual keycode path, like a physical keyboard."""
    import Quartz
    for down in (True, False):
        ev = Quartz.CGEventCreateKeyboardEvent(None, 49, down)
        Quartz.CGEventPost(Quartz.kCGHIDEventTap, ev)
        time.sleep(0.05)


def lua_window_visible(sess) -> bool:
    return bool(sess.eval(
        'local h = reaper.JS_Window_Find("ReaBeat", true) '
        'if h and reaper.JS_Window_IsVisible(h) then return true end '
        'return false'))


def toggle_window(sess):
    sess.eval('reaper.Main_OnCommand('
              'reaper.NamedCommandLookup("_ReaBeat_ShowWindow"), 0)')
    time.sleep(1.0)


def marker_srcpos_list(sess, track_idx: int = 0, item_idx: int = 0):
    return sess.eval(f"""
      local tr = reaper.GetTrack(0, {track_idx})
      if not tr then return {{}} end
      local it = reaper.GetTrackMediaItem(tr, {item_idx})
      if not it then return {{}} end
      local tk = reaper.GetActiveTake(it)
      if not tk then return {{}} end
      local n = reaper.GetTakeNumStretchMarkers(tk)
      local out = {{}}
      for i = 0, n - 1 do
        local _, pos, srcpos = reaper.GetTakeStretchMarker(tk, i)
        out[#out + 1] = srcpos
      end
      return out
    """) or []


# ---------------------------------------------------------------------------
# Module session
# ---------------------------------------------------------------------------
@pytest.fixture(scope="module")
def sess(tmp_path_factory):
    with ReaperSession("reabeat-e2e", extensions=[DYLIB, ORT]) as s:
        # js_ReaScriptAPI must be present in the isolated profile
        if not bool(s.eval('return reaper.APIExists("JS_Window_Find")')):
            pytest.skip("js_ReaScriptAPI not available in isolated profile")
        yield s


@pytest.fixture(scope="module")
def clickwav(tmp_path_factory) -> Path:
    p = tmp_path_factory.mktemp("media") / "clicktrack.wav"
    make_clicktrack(p)
    return p


# ---------------------------------------------------------------------------
# F1 — window toggle lifecycle
# ---------------------------------------------------------------------------
COVERS = ["F1", "F2", "F3", "F4", "F5"]


def test_f1_window_toggle(sess):
    assert not lua_window_visible(sess), "window must start closed"
    toggle_window(sess)
    assert lua_window_visible(sess), "toggle #1 must open the ReaBeat window"
    toggle_window(sess)
    # honest oracle: hidden (SW_HIDE) or destroyed both count as not-visible
    assert not lua_window_visible(sess), "toggle #2 must hide the window"


# ---------------------------------------------------------------------------
# F2 — keyboard reaches REAPER while the ReaBeat window is closed.
# On macOS the original symptom (Windows accelerator bypass) may not
# reproduce; this guards the v2.0.3 invariant indirectly: with the pump
# gated off (window hidden), REAPER must own the keyboard completely.
# ---------------------------------------------------------------------------
def test_f2_keyboard_with_window_closed(sess):
    assert not lua_window_visible(sess)
    import subprocess
    subprocess.run(["osascript", "-e",
                    'tell application "System Events" to set frontmost of '
                    f'(first process whose unix id is {sess.handle.pid}) to true'],
                   capture_output=True)
    time.sleep(0.6)
    assert int(sess.eval("return reaper.GetPlayState()")) == 0
    press_space()            # Space = transport play
    time.sleep(0.8)
    playing = int(sess.eval("return reaper.GetPlayState()"))
    press_space()            # stop again regardless
    time.sleep(0.5)
    sess.eval("reaper.Main_OnCommand(1016, 0)")  # Transport: Stop (belt+braces)
    assert playing & 1, ("Space did not start playback - REAPER did not "
                         "receive the key while ReaBeat's window was closed")


# ---------------------------------------------------------------------------
# F3 — srcpos convention on a trimmed item (v2.0.3 C1)
# ---------------------------------------------------------------------------
def _setup_trimmed_item(sess, wav: Path):
    sess.eval(f"""
      reaper.InsertTrackAtIndex(0, true)
      local tr = reaper.GetTrack(0, 0)
      reaper.SetOnlyTrackSelected(tr)
      reaper.SetEditCurPos(0.0, false, false)
      reaper.InsertMedia([[{wav}]], 0)
      local it = reaper.GetTrackMediaItem(tr, 0)
      local tk = reaper.GetActiveTake(it)
      reaper.SetMediaItemTakeInfo_Value(tk, "D_STARTOFFS", {STARTOFFS})
      reaper.SetMediaItemInfo_Value(it, "D_POSITION", 0.0)
      reaper.SetMediaItemInfo_Value(it, "D_LENGTH", {CLIP_SEC - STARTOFFS})
      reaper.SelectAllMediaItems(0, false)
      reaper.SetMediaItemSelected(it, true)
      reaper.UpdateArrange()
      return true
    """)
    got = sess.eval("""
      local it = reaper.GetTrackMediaItem(reaper.GetTrack(0,0), 0)
      local tk = reaper.GetActiveTake(it)
      return {reaper.GetMediaItemTakeInfo_Value(tk, "D_STARTOFFS"),
              reaper.GetMediaItemInfo_Value(it, "D_LENGTH")}
    """)
    assert abs(got[0] - STARTOFFS) < 1e-6, "slip-edit did not stick"


# Radio rows relative to the pixel-located Apply top (resized() spacings):
# last radio (always the one directly above Apply's 8 pt gap): -8-28+11
LAST_RADIO_ABOVE_APPLY = -25
# tempoMap radio while STRETCH layout shown: stretch radio + 4 option rows
# (30+28+28+30) + gap 8 above Apply -> -(8+30+28+28+30+28)+11
TEMPOMAP_RADIO_IN_STRETCH_LAYOUT = -169


def _wait_apply(ui, timeout: float = 60.0):
    """Detection completion == results shown == Apply bar visible."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        loc = ui.locate_apply()
        if loc:
            return loc
        time.sleep(3.0)
    return None


def _drive_detection_and_apply(sess, timeout: float = 90.0) -> list[float]:
    """Click Detect, wait for the Apply bar (pixel-located), switch to
    Insert Stretch Markers (radio addressed relative to Apply), Apply."""
    ui = Ui(sess)
    shot(sess, "00-before-detect")
    ui.click(DETECT)                       # activation + Detect
    # Since the Cancel feature the button stays ENABLED during detection
    # and a second click CANCELS it - the old blind "safety" re-click did
    # exactly that (incidentally proving the Cancel path works end to end).
    # Re-click only if the first click demonstrably did not start detection
    # (Apply bar absent after a window comfortably longer than this clip's
    # full detection).
    loc = _wait_apply(ui, timeout=8.0)
    if loc is None:
        ui.click(DETECT, activate_first=False)
        loc = _wait_apply(ui, timeout)
    shot(sess, "01-post-detect")
    assert loc is not None, "detection did not finish (Apply bar never appeared)"

    ui.click_frame(141, loc[2] + LAST_RADIO_ABOVE_APPLY)   # stretch radio
    time.sleep(0.6)
    loc = ui.locate_apply()                # layout changed - re-locate
    shot(sess, "02-stretch-mode")
    assert loc is not None, "Apply bar lost after selecting Stretch Markers"
    ui.click_frame(loc[0], loc[1])         # Apply
    time.sleep(1.5)
    shot(sess, "03-post-apply")
    return marker_srcpos_list(sess)


def test_f3_srcpos_convention_on_trimmed_item(sess, clickwav):
    _setup_trimmed_item(sess, clickwav)
    toggle_window(sess)
    assert lua_window_visible(sess)
    time.sleep(1.5)   # item-poll tick + model load ("Ready")

    src = _drive_detection_and_apply(sess)
    assert len(src) >= 8, (
        f"detection/apply produced only {len(src)} stretch markers - "
        "UI drive or detection failed (see session artifacts)")

    # Interval sanity: the subject detected the 0.5 s click grid at all.
    iv = np.diff(sorted(src))
    good_iv = np.sum(np.abs(iv - BPS) < 0.08)
    assert good_iv >= 0.7 * len(iv), (
        f"markers do not follow the 0.5 s click grid (intervals={iv[:12]}...) "
        "- detection quality problem, srcpos verdict withheld")

    # THE oracle: srcpos is SOURCE-ABSOLUTE. Clicks live at k*0.5 in the
    # source file; the old bug shifted every srcpos by D_STARTOFFS=1.75,
    # i.e. exactly 0.25 s off-grid (max distance). Tolerance: 60 ms.
    def assert_on_grid(values):
        offgrid = [v for v in values
                   if min(v % BPS, BPS - (v % BPS)) > TOL]
        assert not offgrid, (
            f"{len(offgrid)}/{len(values)} srcpos off the source click grid "
            f"by >{TOL*1000:.0f} ms, e.g. {offgrid[:6]} - srcpos is NOT "
            "source-absolute (takeOffset bug?)")

    # Mutation-verification: the same assertion must turn RED on data
    # shaped like the OLD bug (every srcpos shifted by D_STARTOFFS).
    rep = mutation_check(
        list(src), lambda s: assert_on_grid(s),
        [("old-bug: srcpos += D_STARTOFFS",
          lambda s: [v + STARTOFFS for v in s]),
         ("uniform 100 ms drift",
          lambda s: [v + 0.1 for v in s])])
    assert rep.clean_passed, rep
    assert all(o.killed for o in rep.outcomes), f"vacuous oracle: {rep}"


# ---------------------------------------------------------------------------
# F4 — tempo map (constant): marker lands inside the cleared range
# ---------------------------------------------------------------------------
def test_f4_tempomap_constant_in_range(sess):
    if len(marker_srcpos_list(sess)) < 8:
        pytest.skip("F3 did not produce a detection to reuse")
    ui = Ui(sess)
    # Switch Stretch -> Tempo Map (radio addressed relative to Apply)
    loc = ui.locate_apply()
    assert loc is not None, "Apply bar not visible entering F4"
    ui.click_frame(141, loc[2] + TEMPOMAP_RADIO_IN_STRETCH_LAYOUT)
    time.sleep(0.6)
    loc = ui.locate_apply()
    shot(sess, "f4-tempomap-mode")
    assert loc is not None, "Apply bar lost after selecting Tempo Map"
    ui.click_frame(loc[0], loc[1])
    time.sleep(1.5)

    markers = sess.eval("""
      local n = reaper.CountTempoTimeSigMarkers(0)
      local out = {}
      for i = 0, n - 1 do
        local ok, pos, _, _, bpm, tsn, tsd = reaper.GetTempoTimeSigMarker(0, i)
        out[#out+1] = {pos, bpm, tsn, tsd}
      end
      return out
    """) or []
    assert len(markers) >= 1, "tempo map Apply inserted no markers"
    item_end = CLIP_SEC - STARTOFFS
    for pos, bpm, tsn, tsd in markers:
        assert -0.05 <= pos <= item_end + 0.05, (
            f"tempo marker at {pos:.3f}s outside the item range 0..{item_end:.2f} "
            "(v2.0.3 range-clip regression)")
        assert 60.0 <= bpm <= 240.0, f"absurd tempo {bpm}"
    # constant mode: expected ~120 BPM (detected dotted grid = 0.5 s)
    assert abs(markers[0][1] - 120.0) < 6.0, f"expected ~120 BPM, got {markers[0][1]}"
    STATE["f4_done"] = True   # action mode is now Tempo Map (F5 relies on this)


# ---------------------------------------------------------------------------
# F5 — GUID guard: detection started on A must not follow selection to B
# ---------------------------------------------------------------------------
def test_f5_guid_guard(sess, clickwav, tmp_path_factory):
    if not STATE.get("f4_done"):
        pytest.skip("depends on F4 (leaves the UI in Tempo Map mode)")
    if not lua_window_visible(sess):
        toggle_window(sess)
    # Second media file (different tempo so a mixed-up result is detectable)
    wav_b = tmp_path_factory.mktemp("media_b") / "clicktrack_b.wav"
    make_clicktrack(wav_b, bpm=100.0, seconds=20.0)
    sess.eval(f"""
      local tr = reaper.GetTrack(0, 0)
      reaper.SetOnlyTrackSelected(tr)
      reaper.SetEditCurPos(45.0, false, false)
      reaper.InsertMedia([[{wav_b}]], 0)
      return true
    """)
    # Select A, start detection, then immediately switch selection to B.
    sess.eval("""
      local tr = reaper.GetTrack(0, 0)
      reaper.SelectAllMediaItems(0, false)
      reaper.SetMediaItemSelected(reaper.GetTrackMediaItem(tr, 0), true)
      return true
    """)
    time.sleep(1.0)  # let ReaBeat's 200 ms item poll pick up A
    ui = Ui(sess)
    ui.click(DETECT)
    ui.click(DETECT, activate_first=False)
    time.sleep(1.5)  # detection of A now running
    sess.eval("""
      local tr = reaper.GetTrack(0, 0)
      reaper.SelectAllMediaItems(0, false)
      reaper.SetMediaItemSelected(reaper.GetTrackMediaItem(tr, 1), true)
      return true
    """)
    time.sleep(15.0)  # let A's detection finish while B is selected (~2 s here)

    # Oracle 1: B untouched - no stretch markers, playrate 1.0.
    b_state = sess.eval("""
      local it = reaper.GetTrackMediaItem(reaper.GetTrack(0,0), 1)
      local tk = reaper.GetActiveTake(it)
      return {reaper.GetTakeNumStretchMarkers(tk),
              reaper.GetMediaItemTakeInfo_Value(tk, "D_PLAYRATE")}
    """)
    assert int(b_state[0]) == 0, "detection of A left stretch markers on B"
    assert abs(b_state[1] - 1.0) < 1e-9, "detection of A changed B's playrate"

    # Oracle 2: A's result was cached under A - re-selecting A restores the
    # detection (radios visible) and Apply produces markers on A WITHOUT a
    # new Detect run.
    sess.eval("""
      local tr = reaper.GetTrack(0, 0)
      reaper.SelectAllMediaItems(0, false)
      reaper.SetMediaItemSelected(reaper.GetTrackMediaItem(tr, 0), true)
      return true
    """)
    time.sleep(1.5)
    # A currently HAS markers from F3/F4 - clear them so the oracle is fresh
    sess.eval("""
      local it = reaper.GetTrackMediaItem(reaper.GetTrack(0,0), 0)
      local tk = reaper.GetActiveTake(it)
      local n = reaper.GetTakeNumStretchMarkers(tk)
      if n > 0 then reaper.DeleteTakeStretchMarkers(tk, 0, n) end
      reaper.UpdateArrange()
      return true
    """)
    assert len(marker_srcpos_list(sess)) == 0
    # Cached restore shows results again (Apply visible); the stretch radio
    # is the last radio above Apply in the tempo-map layout too.
    loc = _wait_apply(ui, 20.0)
    shot(sess, "f5-cached-restore")
    assert loc is not None, ("cached detection for A did not restore on "
                             "re-select (no Apply bar)")
    ui.click_frame(141, loc[2] + LAST_RADIO_ABOVE_APPLY)
    time.sleep(0.6)
    loc = ui.locate_apply()
    assert loc is not None
    ui.click_frame(loc[0], loc[1])
    time.sleep(2.0)
    src = marker_srcpos_list(sess)
    assert len(src) >= 8, (
        "cached detection for A did not restore on re-select "
        "(Apply produced no markers without a fresh Detect)")
