"""Abstract base for simulation modes."""

from __future__ import annotations

from abc import ABC, abstractmethod

from simulation.sim_engine import SimEngine


class SimulationMode(ABC):
    """All simulation modes implement this interface."""

    @abstractmethod
    def run(self, engine: SimEngine) -> None:
        raise NotImplementedError
