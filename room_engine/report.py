"""
REPORT: show what the analysis found.

Writes two files next to the render:
  <name>_room.json  the numbers (for reading or comparing between sources)
  <name>_room.png   four plots: per-band decay, averaged spectrum with
                    detected peaks, the synthetic IR, and the balance meter

These are debug views for judging the algorithm, not a preset format.
"""

import json

import numpy as np


def summary_text(analysis, balance_used):
    """Short plain-English summary printed to the terminal."""
    a = analysis
    conf_t60 = [t for t, c in zip(a.band_t60_s, a.band_confident) if c]
    lines = []
    if conf_t60:
        lines.append(f"  Decay (T60):  {min(conf_t60):.2f}s to {max(conf_t60):.2f}s "
                     f"across {len(conf_t60)}/{len(a.band_t60_s)} measurable bands "
                     f"(median {np.median(conf_t60):.2f}s)")
    else:
        lines.append("  Decay (T60):  no measurable decay (steady sound); using fallback tail")
    if a.peaks:
        lines.append(f"  Resonances:   {len(a.peaks)} peak(s)")
        for p in a.peaks:
            lines.append(f"                {p.freq_hz:8.1f} Hz   +{p.gain_db:4.1f} dB   Q {p.q:4.1f}")
    else:
        lines.append("  Resonances:   none detected")
    lines.append(f"  Late energy:  {a.late_energy_fraction * 100:.0f}% arrives after 50 ms")
    detected = a.balance
    tag = "detected" if abs(balance_used - detected) < 1e-9 else f"OVERRIDDEN (detected {detected:.2f})"
    lines.append(f"  Balance:      {balance_used:.2f}  [0 = tonal, 1 = spatial]  {tag}")
    return "\n".join(lines)


def write_json(path, analysis, balance_used, settings):
    data = analysis.to_dict()
    # The spectrum arrays are long; keep them, but round them for readability.
    data["spectrum_db"] = [round(v, 2) for v in data["spectrum_db"]]
    data["spectrum_freqs_hz"] = [round(v, 1) for v in data["spectrum_freqs_hz"]]
    data["balance_used"] = balance_used
    data["settings"] = settings
    with open(path, "w") as f:
        json.dump(data, f, indent=2)


def write_plot(path, room):
    """Four-panel overview image. Skipped quietly if matplotlib is missing."""
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        return False

    a = room.analysis
    fig, axes = plt.subplots(2, 2, figsize=(12, 7))

    # Per-band decay
    ax = axes[0, 0]
    colors = ["#3b6ea8" if c else "#b0b0b0" for c in a.band_confident]
    ax.bar(range(len(a.band_t60_s)), a.band_t60_s, color=colors)
    ax.set_xticks(range(len(a.band_t60_s)))
    ax.set_xticklabels([f"{c:.0f}" if c < 1000 else f"{c / 1000:.1f}k"
                        for c in a.band_centers_hz], rotation=60, fontsize=7)
    ax.set_title("Per-band decay T60 (grey = not measurable, fallback)")
    ax.set_xlabel("Band center (Hz)")
    ax.set_ylabel("Seconds")

    # Spectrum + peaks
    ax = axes[0, 1]
    ax.semilogx(a.spectrum_freqs_hz, a.spectrum_db, color="#333")
    for p in a.peaks:
        y = np.interp(p.freq_hz, a.spectrum_freqs_hz, a.spectrum_db)
        ax.plot(p.freq_hz, y, "o", color="#c0392b")
        ax.annotate(f"{p.freq_hz:.0f}", (p.freq_hz, y), textcoords="offset points",
                    xytext=(0, 6), ha="center", fontsize=7, color="#c0392b")
    ax.set_title("Averaged spectrum (tonal fingerprint) + resonant peaks")
    ax.set_xlabel("Hz")
    ax.set_ylabel("dB")

    # IR
    ax = axes[1, 0]
    t = np.arange(room.ir.shape[1]) / room.sr
    ax.plot(t, room.ir[0], lw=0.4, color="#3b6ea8")
    ax.set_title("Synthetic impulse response (left channel)")
    ax.set_xlabel("Seconds")

    # Balance meter
    ax = axes[1, 1]
    ax.barh([0], [1.0], color="#eee")
    ax.barh([0], [room.balance], color="#3b6ea8")
    ax.axvline(a.balance, color="#c0392b", ls="--", label=f"detected {a.balance:.2f}")
    ax.set_xlim(0, 1)
    ax.set_yticks([])
    ax.set_xticks([0, 0.5, 1])
    ax.set_xticklabels(["tonal", "0.5", "spatial"])
    d = a.balance_details
    ax.set_title(f"Balance {room.balance:.2f}  (spatial evidence {d['spatial_evidence']:.2f}, "
                 f"tonal evidence {d['tonal_evidence']:.2f})", fontsize=9)
    ax.legend(loc="upper left", fontsize=8)

    fig.tight_layout()
    fig.savefig(path, dpi=110)
    plt.close(fig)
    return True
