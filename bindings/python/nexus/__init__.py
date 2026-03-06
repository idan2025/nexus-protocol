"""NEXUS Protocol -- Python bindings for libnexus."""

from .identity import Identity
from .node import Node
from .transport import registry_init, create_pipe_pair, set_active
from .errors import NexusError, check

__version__ = "0.1.0"
__all__ = [
    "Identity",
    "Node",
    "NexusError",
    "check",
    "registry_init",
    "create_pipe_pair",
    "set_active",
]
