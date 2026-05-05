"""Gait cycle detection via autocorrelation."""

from __future__ import annotations

import numpy as np


def detect_gait_cycle(positions: np.ndarray) -> int | None:
    """Find the gait period via autocorrelation on the first joint's position.

    Args:
        positions: Array of shape ``(num_steps, num_joints)``.

    Returns:
        The period in steps, or ``None`` if no clear cycle is found.
    """
    sig = positions[:, 0].copy()
    sig -= sig.mean()
    std = sig.std()
    if std < 1e-8:
        return None
    sig /= std

    n = len(sig)
    autocorr = np.correlate(sig, sig, mode="full")[n - 1 :]
    autocorr /= autocorr[0]

    min_lag = max(5, n // 50)
    for i in range(1, len(autocorr)):
        if autocorr[i] < 0:
            min_lag = max(min_lag, i)
            break

    search = autocorr[min_lag:]
    if len(search) == 0:
        return None
    peak_idx = int(np.argmax(search)) + min_lag
    if autocorr[peak_idx] < 0.3:
        return None
    return peak_idx
