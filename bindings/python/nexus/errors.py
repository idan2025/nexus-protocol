"""NEXUS Protocol -- Error handling."""


class NexusError(Exception):
    def __init__(self, code, message=""):
        self.code = code
        super().__init__(f"NX error {code}: {message}")


ERROR_MAP = {
    -1: "Invalid argument",
    -2: "Buffer too small",
    -3: "Crypto failure",
    -4: "Auth failure",
    -5: "No memory",
    -6: "Not found",
    -7: "Transport error",
    -8: "Timeout",
    -9: "Full",
    -10: "Already exists",
    -11: "IO error",
}


def check(rc):
    """Raise NexusError if rc != 0."""
    if rc != 0:
        raise NexusError(rc, ERROR_MAP.get(rc, "Unknown"))
