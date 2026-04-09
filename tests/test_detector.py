"""Tests for REABeat beat detector."""

import numpy as np
import pytest

from reabeat.detector import (
    DetectionResult,
    _compute_confidence,
    _compute_downbeats,
    _compute_peaks,
    _compute_tempo,
    _estimate_time_signature,
    check_backend,
)


class TestCheckBackend:
    def test_returns_tuple(self):
        ok, msg = check_backend()
        assert isinstance(ok, bool)
        assert isinstance(msg, str)

    def test_message_not_empty(self):
        _, msg = check_backend()
        assert len(msg) > 0


class TestComputeTempo:
    def test_120_bpm(self):
        from reabeat.config import DetectionConfig
        beats = np.arange(0, 10, 0.5)  # 0.5s intervals = 120 BPM
        tempo = _compute_tempo(beats, DetectionConfig())
        assert abs(tempo - 120.0) < 1.0

    def test_90_bpm(self):
        from reabeat.config import DetectionConfig
        beats = np.arange(0, 10, 60.0 / 90)
        tempo = _compute_tempo(beats, DetectionConfig())
        assert abs(tempo - 90.0) < 1.0

    def test_too_few_beats(self):
        from reabeat.config import DetectionConfig
        beats = np.array([1.0])
        tempo = _compute_tempo(beats, DetectionConfig())
        assert tempo == 120.0

    def test_filters_outliers(self):
        from reabeat.config import DetectionConfig
        beats = np.array([0, 0.5, 1.0, 1.5, 5.0, 5.5, 6.0])
        tempo = _compute_tempo(beats, DetectionConfig())
        assert abs(tempo - 120.0) < 5.0


class TestTimeSignature:
    def test_default_4_4(self):
        beats = np.arange(0, 10, 0.5)
        num, denom = _estimate_time_signature(beats, 120.0)
        assert num == 4
        assert denom == 4

    def test_too_few_beats(self):
        beats = np.array([0, 0.5, 1.0])
        num, denom = _estimate_time_signature(beats, 120.0)
        assert num == 4


class TestDownbeats:
    def test_4_4(self):
        beats = list(range(16))
        downbeats = _compute_downbeats(np.array(beats, dtype=float), 4)
        assert downbeats == [0.0, 4.0, 8.0, 12.0]

    def test_3_4(self):
        beats = list(range(12))
        downbeats = _compute_downbeats(np.array(beats, dtype=float), 3)
        assert downbeats == [0.0, 3.0, 6.0, 9.0]

    def test_empty(self):
        assert _compute_downbeats(np.array([]), 4) == []


class TestConfidence:
    def test_perfect_tempo(self):
        beats = np.arange(0, 10, 0.5)
        conf = _compute_confidence(beats, 120.0)
        assert conf > 0.9

    def test_inconsistent(self):
        beats = np.array([0, 0.3, 0.9, 1.1, 2.0, 2.8])
        conf = _compute_confidence(beats, 120.0)
        assert conf < 0.7

    def test_too_few(self):
        beats = np.array([0, 0.5])
        conf = _compute_confidence(beats, 120.0)
        assert conf == 0.5


class TestPeaks:
    def test_basic(self):
        sr = 22050
        y = np.sin(2 * np.pi * 440 * np.arange(sr) / sr).astype(np.float32)
        peaks = _compute_peaks(y, sr, 100)
        assert len(peaks) == 100
        assert all(0 <= p <= 1.0 for p in peaks)

    def test_silent(self):
        y = np.zeros(22050, dtype=np.float32)
        peaks = _compute_peaks(y, 22050, 100)
        assert all(p == 0.0 for p in peaks)

    def test_short(self):
        y = np.array([0.5, -0.5], dtype=np.float32)
        peaks = _compute_peaks(y, 22050, 100)
        assert len(peaks) >= 1


class TestDetectionResult:
    def test_fields(self):
        r = DetectionResult(
            beats=[0.5, 1.0],
            downbeats=[0.5],
            tempo=120.0,
            time_sig_num=4,
            time_sig_denom=4,
            confidence=0.95,
            backend="beat-this",
            duration=10.0,
        )
        assert len(r.beats) == 2
        assert r.tempo == 120.0
        assert r.backend == "beat-this"
        assert r.peaks == []
