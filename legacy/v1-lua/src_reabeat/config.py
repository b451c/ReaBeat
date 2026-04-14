"""Configuration constants for ReaBeat."""

from dataclasses import dataclass

SAMPLE_RATE = 22050
DEFAULT_PORT = 9877


@dataclass
class DetectionConfig:
    """Beat detection parameters."""

    min_bpm: float = 40.0
    max_bpm: float = 240.0
    sample_rate: int = SAMPLE_RATE
    peaks_per_second: int = 100
