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
            "  cd ReaBeat && uv sync --extra neural\n\n"
            "Or:\n"
            "  pip install torch torchaudio"
        )
    try:
        from beat_this.inference import File2Beats  # noqa: F401
    except ImportError:
        return False, (
            "beat-this is not installed.\n\n"
            "Install with:\n"
            "  cd ReaBeat && uv sync --extra neural\n\n"
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
        elif hasattr(torch.backends, "mps") and torch.backends.mps.is_available():
            device = "mps"

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

    # Compute tempo and time signature
    progress("Computing tempo and time signature...", 0.8)
    tempo = _compute_tempo(beats, config)

    # Use beat-this neural downbeats (dedicated model head) instead of
    # naive every-Nth-beat. Falls back to naive if neural returns empty.
    if len(raw_downbeats) >= 2:
        downbeats = raw_downbeats.tolist()
        time_sig_num = _time_sig_from_downbeats(beats, raw_downbeats)
        time_sig_denom = 4
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
# Analysis helpers
# ---------------------------------------------------------------------------
def _compute_tempo(beats: np.ndarray, config: DetectionConfig) -> float:
    """Compute tempo from beat positions.

    Uses two methods and picks the more accurate one:

    1. Total span: (n_beats - 1) * 60 / (last - first). Most precise
       for constant-tempo tracks because it averages out beat-this's
       20ms frame quantization over the full song length.

    2. Filtered mean: mean of inter-beat intervals after removing
       outliers (< 0.15s or > 2.0s). Robust against gaps/silences
       in the audio.

    If both methods agree within 2%, total span is used (higher
    precision). Otherwise filtered mean is used (outlier-robust).
    """
    if len(beats) < 2:
        return 120.0

    def _octave_correct(bpm: float) -> float:
        while bpm < config.min_bpm:
            bpm *= 2
        while bpm > config.max_bpm:
            bpm /= 2
        return bpm

    # Method 1: total span
    total_span = float(beats[-1] - beats[0])
    if total_span <= 0:
        return 120.0
    span_bpm = _octave_correct((len(beats) - 1) * 60.0 / total_span)

    # Method 2: filtered mean of inter-beat intervals
    ibis = np.diff(beats)
    ibis = ibis[(ibis > 0.15) & (ibis < 2.0)]
    if len(ibis) == 0:
        return span_bpm
    mean_bpm = _octave_correct(60.0 / float(np.mean(ibis)))

    # Pick: total span if consistent, filtered mean if gaps/outliers
    if abs(span_bpm - mean_bpm) / mean_bpm < 0.02:
        return span_bpm
    return mean_bpm


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
