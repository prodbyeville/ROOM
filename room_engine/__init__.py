"""
Room Synthesis Engine (prototype).

Analyze any recording, derive a "room" from it (per-band decay + resonant
character), and apply that room to other audio.

The pipeline is split into small modules so each stage can be read and
tweaked on its own:

    audio_io.py   loading / saving audio
    analysis.py   recording -> RoomAnalysis (measurements)
    synthesis.py  RoomAnalysis -> Room (synthetic IR + resonant filters)
    process.py    Room + target audio -> processed audio
    report.py     human-readable JSON + plot of what was detected
"""

from .analysis import analyze_room, RoomAnalysis
from .synthesis import synthesize_room, Room
from .process import apply_room

__all__ = [
    "analyze_room",
    "RoomAnalysis",
    "synthesize_room",
    "Room",
    "apply_room",
]
