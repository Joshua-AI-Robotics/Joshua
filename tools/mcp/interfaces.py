"""Small read-only backend contracts; closing adapters never stops a runtime."""

from abc import ABC, abstractmethod


class SetupInspector(ABC):
    @abstractmethod
    async def list_supported_components(self): ...

    @abstractmethod
    async def validate_config(self, config_path): ...

    @abstractmethod
    async def list_devices(self, config_path): ...

    @abstractmethod
    async def describe_device(self, config_path, device_id): ...

    async def aclose(self):
        pass


class RuntimeObserver(ABC):
    @abstractmethod
    async def get_runtime_status(self): ...

    @abstractmethod
    async def get_recent_logs(self, cursor=None, limit=50, min_level="WARN"): ...

    @abstractmethod
    async def get_feedback(self, sensor_id): ...

    async def aclose(self):
        pass
