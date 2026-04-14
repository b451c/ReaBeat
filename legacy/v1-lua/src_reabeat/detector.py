"""Beat detection using beat-this (CPJKU, ISMIR 2024).

Single backend — no silent fallbacks. Clear errors when something is wrong.
"""

from __future__ import annotations

import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, List, Optional, Tuple

import numpy as np

from reabeat.config import SAMPLE_RATE, DetectionConfig


@dataclass
class DetectionResult:
    """Result of beat detection."""

    beats: List[float]
    downbeats: List[float]
    tempo: float
    time_sig_num: int
    time_sig_denom: int
    confidence: float
    backend: str
    duration: float
    peaks: List[float] = field(default_factory=list)
    detection_time: float = 0.0


def check_backend() -> Tuple[bool, str]:
    """Check if beat-this is installed and working.

    Returns (ok, message) — message explains the problem if not ok.
    """
    try:
        import torch  # noqa: F401
    except ImportError:
        return False, (
            "PyTorch is not installed.\n\n"
            "Install with:\n"
            "  cd ReaBeat && uv sync\n\n"
            "Or:\n"
            "  pip install torch torchaudio"
        )
    try:
        from beat_this.inference import File2Beats  # noqa: F401
    except ImportError:
        return False, (
            "beat-this is not installed.\n\n"
            "Install with:\n"
            "  cd ReaBeat && uv sync\n\n"
            "Or:\n"
            "  pip install git+https://github.com/CPJKU/beat_this.git"
        )
    return True, "beat-this ready"


def detect_beats(
    audio_path: str,
    config: Optional[DetectionConfig] = None,
    on_progress: Optional[Callable[[str, float], None]] = None,
) -> DetectionResult:
    """Detect beats in an audio file using beat-this.

    Raises clear errors if:
    - beat-this not installed (with install instructions)
    - Audio file not found or unreadable
    - Audio too short or silent
    - Detection produces no beats
    """
    config = config or DetectionConfig()
    t0 = time.time()

    def progress(msg: str, frac: float) -> None:
        if on_progress:
            on_progress(msg, frac)

    # Check backend before doing anything
    ok, msg = check_backend()
    if not ok:
        raise RuntimeError(msg)

    # Validate file
    path = Path(audio_path)
    if not path.exists():
        raise FileNotFoundError(f"Audio file not found: {audio_path}")

    # Load audio
    progress("Loading audio...", 0.0)
    try:
        import librosa
        y, sr = librosa.load(audio_path, sr=config.sample_rate, mono=True)
    except Exception as e:
        raise RuntimeError(
            f"Cannot read audio file: {path.name}\n\n"
            f"Error: {e}\n\n"
            "Supported formats: WAV, MP3, FLAC, OGG, AIFF, M4A"
        ) from e

    duration = float(len(y) / sr)
    if duration < 2.0:
        raise RuntimeError(
            f"Audio too short ({duration:.1f}s). Need at least 2 seconds for beat detection."
        )

    rms_total = float(np.sqrt(np.mean(y ** 2)))
    if rms_total < 0.001:
        raise RuntimeError(
            f"Audio appears to be silent (RMS={rms_total:.5f}).\n"
            "Beat detection needs audible musical content."
        )

    # Compute waveform peaks
    progress("Computing waveform...", 0.1)
    peaks = _compute_peaks(y, sr, config.peaks_per_second)

    # Run beat-this
    progress("Detecting beats (beat-this)...", 0.2)
    try:
        import torch
        from beat_this.inference import File2Beats

        device = "cpu"
        if torch.cuda.is_available():
            device = "cuda"

        try:
            f2b = File2Beats(device=device, dbn=False)
            beat_times, _downbeat_times = f2b(audio_path)
        except RuntimeError as gpu_err:
            if device != "cpu":
                # GPU failed (OOM, driver issue) — retry on CPU
                f2b = File2Beats(device="cpu", dbn=False)
                beat_times, _downbeat_times = f2b(audio_path)
            else:
                raise gpu_err
        beats = np.array(beat_times, dtype=np.float64)
        raw_downbeats = np.array(_downbeat_times, dtype=np.float64)
    except (ImportError, ModuleNotFoundError):
        # Should not happen — check_backend() already verified
        raise
    except Exception as e:
        raise RuntimeError(
            f"beat-this failed on {path.name}:\n\n"
            f"{type(e).__name__}: {e}\n\n"
            "This may happen with very unusual audio (non-music, extreme distortion, "
            "or corrupt files). Try a different file."
        ) from e

    if len(beats) < 2:
        raise RuntimeError(
            f"No beats detected in {path.name}.\n\n"
            "The file may be silent, non-musical, or too short. "
            "beat-this needs rhythmic content to detect beats."
        )

    # Refine beat positions to nearest audio onset (transient attack).
    # beat-this detects at 20ms resolution — onsets snap to the actual
    # attack within ±30ms for sample-accurate marker placement.
    progress("Refining beat positions...", 0.7)
    beats = _refine_to_onsets(y, sr, beats)
    raw_downbeats = _refine_to_onsets(y, sr, raw_downbeats)

    # Compute tempo and time signature
    progress("Computing tempo and time signature...", 0.8)
    tempo = _compute_tempo(beats, config)

    # Use beat-this neural downbeats (dedicated model head) instead of
    # naive every-Nth-beat. Falls back to naive if neural returns empty.
    if len(raw_downbeats) >= 2:
        time_sig_num = _time_sig_from_downbeats(beats, raw_downbeats)
        time_sig_denom = 4
        # Clean up noisy neural downbeats: remove outliers, fill gaps
        downbeats = _clean_downbeats(raw_downbeats, tempo, time_sig_num)
    else:
        time_sig_num, time_sig_denom = _estimate_time_signature(beats, tempo)
        downbeats = _compute_downbeats(beats, time_sig_num)

    confidence = _compute_confidence(beats, tempo)

    progress("Done.", 1.0)
    detection_time = time.time() - t0

    return DetectionResult(
        beats=[round(b, 6) for b in beats.tolist()],
        downbeats=[round(d, 6) for d in downbeats],
        tempo=round(tempo, 2),
        time_sig_num=time_sig_num,
        time_sig_denom=time_sig_denom,
        confidence=round(confidence, 3),
        backend="beat-this",
        duration=round(duration, 3),
        peaks=peaks,
        detection_time=round(detection_time, 2),
    )


# ---------------------------------------------------------------------------
# Downbeat cleaning
# ---------------------------------------------------------------------------
def _clean_downbeats(
    raw_downbeats: np.ndarray, tempo: float, time_sig_num: int
) -> List[float]:
    """Clean up noisy neural downbeats.

    Neural downbeats from beat-this can be inconsistent: extra downbeats
    placed too close together, or missing downbeats creating gaps.
    This filters outliers and fills gaps using the detected tempo.
    """
    if len(raw_downbeats) < 2:
        return raw_downbeats.tolist()

    expected_bar = 60.0 * time_sig_num / tempo
    intervals = np.diff(raw_downbeats)
    median_interval = float(np.median(intervals))

    # Use whichever is more reliable: median interval or tempo-derived
    if abs(median_interval - expected_bar) / expected_bar < 0.15:
        ref_interval = expected_bar
    else:
        ref_interval = median_interval

    # Filter: keep only downbeats that create reasonable bar durations
    cleaned = [float(raw_downbeats[0])]
    for i in range(1, len(raw_downbeats)):
        gap = raw_downbeats[i] - cleaned[-1]
        ratio = gap / ref_interval

        if ratio < 0.6:
            # Too close — skip this downbeat (erroneous extra)
            continue
        elif ratio > 1.6:
            # Too far — fill in missing downbeats
            n_missing = round(ratio) - 1
            for j in range(1, n_missing + 1):
                filled = cleaned[-1] + ref_interval * j / (n_missing + 1)
                cleaned.append(filled)
            cleaned.append(float(raw_downbeats[i]))
        else:
            # Normal — keep
            cleaned.append(float(raw_downbeats[i]))

    return cleaned


# ---------------------------------------------------------------------------
# Onset refinement
# ---------------------------------------------------------------------------
def _refine_to_onsets(
    y: np.ndarray, sr: int, positions: np.ndarray, window_sec: float = 0.030
) -> np.ndarray:
    """Snap each beat position to the nearest audio onset (transient attack).

    Uses librosa onset detection for reliable transient finding, then
    snaps each beat-this position to the nearest onset within ±window_sec.
    """
    if len(positions) == 0:
        return positions

    import librosa

    # Detect onsets at high resolution (~3ms per frame)
    hop = 64
    onset_env = librosa.onset.onset_strength(y=y, sr=sr, hop_length=hop)
    onset_frames = librosa.onset.onset_detect(
        onset_envelope=onset_env, sr=sr, hop_length=hop, units="frames")
    onset_times = librosa.frames_to_time(onset_frames, sr=sr, hop_length=hop)

    if len(onset_times) == 0:
        return positions

    refined = np.copy(positions)
    for i, pos in enumerate(positions):
        # Find nearest onset within window
        diffs = onset_times - pos
        in_window = np.abs(diffs) <= window_sec
        if not np.any(in_window):
            continue
        candidates = np.where(in_window)[0]
        nearest = candidates[np.argmin(np.abs(diffs[candidates]))]
        refined[i] = onset_times[nearest]

    return refined


# ---------------------------------------------------------------------------
# Analysis helpers
# ---------------------------------------------------------------------------
def _compute_tempo(beats: np.ndarray, config: DetectionConfig) -> float:
    """Compute tempo from beat positions using phase-aware regression.

    Based on CPJKU/beat_this#13 (Paulllux): circular mean for optimal
    phase + linear regression on beat grid for precise BPM. Falls back
    to span/mean method if regression fails.

    Octave correction uses 78-185 BPM range (optimal for modern music:
    captures 140 BPM trap without breaking 85 BPM hip-hop).
    """
    if len(beats) < 2:
        return 120.0

    def _octave_correct(bpm: float) -> float:
        if bpm == 0:
            return 120.0
        while bpm < 78:
            bpm *= 2
        while bpm > 185:
            bpm /= 2
        return bpm

    # Trim 15% edges to avoid intro/outro drift
    n = len(beats)
    start_idx = int(n * 0.15)
    end_idx = int(n * 0.85)
    if (end_idx - start_idx) < 4:
        subset = beats
    else:
        subset = beats[start_idx:end_idx]

    # Robust interval estimation
    intervals = np.diff(subset)
    intervals = intervals[(intervals > 0.2) & (intervals < 2.0)]
    if len(intervals) == 0:
        return 120.0

    median_interval = float(np.median(intervals))
    valid = intervals[np.abs(intervals - median_interval) < (median_interval * 0.15)]
    avg_interval = float(np.mean(valid)) if len(valid) > 0 else median_interval
    if avg_interval <= 0:
        return 120.0

    # Phase optimization: find best-fitting grid start via circular mean
    phases = (subset % avg_interval)
    theta = (phases / avg_interval) * 2 * np.pi
    mean_theta = np.arctan2(np.mean(np.sin(theta)), np.mean(np.cos(theta)))
    if mean_theta < 0:
        mean_theta += 2 * np.pi
    optimal_phase = (mean_theta / (2 * np.pi)) * avg_interval

    # Linear regression on beat indices for precise BPM
    raw_indices = (subset - optimal_phase) / avg_interval
    beat_indices = np.round(raw_indices)

    try:
        from scipy.stats import linregress
        slope, _, r_value, _, _ = linregress(beat_indices, subset)
        if slope > 0 and r_value ** 2 > 0.99:
            return _octave_correct(60.0 / slope)
    except (ImportError, Exception):
        pass

    # Fallback: octave-corrected mean
    return _octave_correct(60.0 / avg_interval)


def _estimate_time_signature(
    beats: np.ndarray, tempo: float
) -> Tuple[int, int]:
    """Estimate time signature from beat pattern."""
    if len(beats) < 8:
        return 4, 4
    ibis = np.diff(beats)
    if len(ibis) >= 6:
        ibis_norm = ibis - np.mean(ibis)
        if np.std(ibis_norm) > 0.01:
            corr = np.correlate(ibis_norm, ibis_norm, mode="full")
            corr = corr[len(corr) // 2 :]
            if len(corr) > 4 and corr[3] > corr[4] * 1.3:
                return 3, 4
    return 4, 4


def _time_sig_from_downbeats(
    beats: np.ndarray, downbeats: np.ndarray
) -> int:
    """Derive time signature numerator from neural downbeat spacing.

    Counts beats between consecutive downbeats and returns the most
    common count (typically 4 for 4/4 or 3 for 3/4).
    """
    if len(downbeats) < 2 or len(beats) < 2:
        return 4
    counts = []
    for i in range(len(downbeats) - 1):
        n = int(np.sum((beats >= downbeats[i] - 0.03) &
                       (beats < downbeats[i + 1] - 0.03)))
        if 2 <= n <= 7:
            counts.append(n)
    if not counts:
        return 4
    # Most common count
    from collections import Counter
    return Counter(counts).most_common(1)[0][0]


def _compute_downbeats(
    beats: np.ndarray, time_sig_num: int
) -> List[float]:
    """Fallback: compute downbeat times from beats and time signature."""
    if len(beats) == 0:
        return []
    return [float(beats[i]) for i in range(0, len(beats), time_sig_num)]


def _compute_confidence(beats: np.ndarray, tempo: float) -> float:
    """Compute detection confidence based on tempo consistency."""
    if len(beats) < 4:
        return 0.5
    ibis = np.diff(beats)
    expected_ibi = 60.0 / tempo
    deviations = np.abs(ibis - expected_ibi) / expected_ibi
    consistent = float(np.mean(deviations < 0.10))
    return min(1.0, consistent)


def _compute_peaks(
    y: np.ndarray, sr: int, peaks_per_second: int
) -> List[float]:
    """Downsample audio to RMS envelope for waveform display."""
    hop = max(1, sr // peaks_per_second)
    n_frames = len(y) // hop
    if n_frames == 0:
        return [0.0]
    frames = y[: n_frames * hop].reshape(n_frames, hop)
    rms = np.sqrt(np.mean(frames ** 2, axis=1))
    p98 = float(np.percentile(rms, 98))
    if p98 > 0:
        rms = rms / p98
    rms = np.clip(rms, 0.0, 1.0)
    return [round(float(p), 4) for p in rms]
