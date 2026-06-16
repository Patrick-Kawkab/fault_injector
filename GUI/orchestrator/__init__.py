"""
Fault injection orchestrator
============================
Python layer between the PyQt5 GUI and the C++ injector / hardware.

``OrchestratorWorker`` is a drop-in replacement for ``mock_worker.MockInjectionWorker``
— same signals, same constructor, same ``finished`` payload. The core modules
(targets, injector interface, results) have no Qt dependency and can run headless.
"""

from .injector_interface import InjectorError, InjectorInterface
from .results_manager import ResultsManager
from .targets import BaseTarget, QemuTarget, TargetError, TargetFactory, TivaTarget

try:  # PyQt5 is optional for headless use of the core
    from .worker import OrchestratorWorker
except ImportError:  # pragma: no cover
    OrchestratorWorker = None  # type: ignore

__all__ = [
    "OrchestratorWorker",
    "InjectorInterface", "InjectorError",
    "ResultsManager",
    "BaseTarget", "QemuTarget", "TivaTarget", "TargetFactory", "TargetError",
]
