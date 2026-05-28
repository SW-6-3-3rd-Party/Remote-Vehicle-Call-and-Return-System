import threading
from collections import deque
from typing import Any, Generic, List, TypeVar

T = TypeVar("T")


class RingBuffer(Generic[T]):
    """Thread-safe fixed-size ring buffer (oldest item is dropped on overflow)."""

    def __init__(self, maxlen: int):
        self._buf: deque[T] = deque(maxlen=maxlen)
        self._lock = threading.Lock()

    def push(self, item: T) -> None:
        with self._lock:
            self._buf.append(item)

    def snapshot(self) -> List[T]:
        """Return a stable copy of current contents, oldest → newest."""
        with self._lock:
            return list(self._buf)

    def __len__(self) -> int:
        with self._lock:
            return len(self._buf)
