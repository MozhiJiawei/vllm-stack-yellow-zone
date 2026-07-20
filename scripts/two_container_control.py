#!/usr/bin/env python3
"""Small trusted-network control plane for two-container experiments.

The protocol is newline-delimited JSON over one persistent TCP connection per
participant.  It deliberately carries control messages only; model traffic and
XCCL communicators remain entirely inside their respective containers.
"""

from __future__ import annotations

from dataclasses import dataclass
import json
import queue
import socket
import threading
import time
from typing import Any, Iterable


PROTOCOL_VERSION = 1
MAX_MESSAGE_BYTES = 1024 * 1024


class ControlError(RuntimeError):
    """Raised when the control connection or protocol fails."""


class HandshakeRejected(ControlError):
    """Raised for a permanent handshake mismatch that retry cannot repair."""


class JsonPeer:
    """Thread-safe JSON-lines peer with a background socket reader."""

    def __init__(self, sock: socket.socket, name: str) -> None:
        self.sock = sock
        self.name = name
        self._messages: queue.Queue[dict[str, Any] | BaseException] = queue.Queue()
        self._send_lock = threading.Lock()
        self._closed = threading.Event()
        self._reader = threading.Thread(
            target=self._read_loop, name=f"control-reader-{name}", daemon=True
        )
        self._reader.start()

    def _read_loop(self) -> None:
        buffer = bytearray()
        try:
            while not self._closed.is_set():
                chunk = self.sock.recv(65536)
                if not chunk:
                    raise ControlError(f"control peer {self.name} disconnected")
                buffer.extend(chunk)
                if len(buffer) > MAX_MESSAGE_BYTES and b"\n" not in buffer:
                    raise ControlError(
                        f"control peer {self.name} sent an oversized message"
                    )
                while b"\n" in buffer:
                    raw, _, remainder = buffer.partition(b"\n")
                    buffer = bytearray(remainder)
                    if len(raw) > MAX_MESSAGE_BYTES:
                        raise ControlError(
                            f"control peer {self.name} sent an oversized message"
                        )
                    try:
                        message = json.loads(raw.decode("utf-8"))
                    except (UnicodeDecodeError, json.JSONDecodeError) as error:
                        raise ControlError(
                            f"control peer {self.name} sent invalid JSON: {error}"
                        ) from error
                    if not isinstance(message, dict) or not isinstance(
                        message.get("type"), str
                    ):
                        raise ControlError(
                            f"control peer {self.name} sent an invalid message"
                        )
                    self._messages.put(message)
        except BaseException as error:
            if not self._closed.is_set():
                self._messages.put(error)

    def send(self, message: dict[str, Any]) -> None:
        if self._closed.is_set():
            raise ControlError(f"control peer {self.name} is closed")
        payload = (
            json.dumps(message, separators=(",", ":"), ensure_ascii=True).encode(
                "utf-8"
            )
            + b"\n"
        )
        if len(payload) > MAX_MESSAGE_BYTES:
            raise ControlError("attempted to send an oversized control message")
        try:
            with self._send_lock:
                self.sock.sendall(payload)
        except OSError as error:
            raise ControlError(
                f"failed to send to control peer {self.name}: {error}"
            ) from error

    def recv(self, timeout: float | None = None) -> dict[str, Any]:
        try:
            item = self._messages.get(timeout=timeout)
        except queue.Empty:
            raise TimeoutError from None
        if isinstance(item, BaseException):
            raise ControlError(str(item)) from item
        return item

    def recv_nowait(self) -> dict[str, Any]:
        return self.recv(timeout=0)

    def close(self) -> None:
        if self._closed.is_set():
            return
        self._closed.set()
        try:
            self.sock.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        self.sock.close()


def parse_endpoint(value: str) -> tuple[str, int]:
    """Parse HOST:PORT, including bracketed IPv6 addresses."""
    if value.startswith("["):
        host, separator, port_text = value[1:].partition("]:")
    else:
        host, separator, port_text = value.rpartition(":")
    if not separator or not host:
        raise ValueError("endpoint must be HOST:PORT")
    try:
        port = int(port_text)
    except ValueError as error:
        raise ValueError("endpoint port must be an integer") from error
    if not 1 <= port <= 65535:
        raise ValueError("endpoint port must be between 1 and 65535")
    return host, port


def connect_participant(
    endpoint: str,
    experiment: str,
    run_id: str,
    role: str,
    timeout: float,
) -> JsonPeer:
    """Connect with retry, perform the participant handshake, and return a peer."""
    host, port = parse_endpoint(endpoint)
    deadline = time.monotonic() + timeout
    last_error: BaseException | None = None
    while time.monotonic() < deadline:
        sock: socket.socket | None = None
        peer: JsonPeer | None = None
        try:
            remaining = max(0.1, deadline - time.monotonic())
            sock = socket.create_connection((host, port), timeout=min(5.0, remaining))
            sock.settimeout(None)
            peer = JsonPeer(sock, f"coordinator-for-{role}")
            peer.send(
                {
                    "type": "HELLO",
                    "protocol": PROTOCOL_VERSION,
                    "experiment": experiment,
                    "run_id": run_id,
                    "role": role,
                }
            )
            reply = peer.recv(timeout=min(5.0, remaining))
            if reply.get("type") == "HELLO_ACK":
                return peer
            if reply.get("type") == "REJECT":
                raise HandshakeRejected(str(reply.get("reason", "handshake rejected")))
            raise ControlError(f"unexpected handshake reply: {reply.get('type')}")
        except HandshakeRejected:
            if peer is not None:
                peer.close()
            elif sock is not None:
                sock.close()
            raise
        except (OSError, TimeoutError, ControlError) as error:
            last_error = error
            if peer is not None:
                peer.close()
            elif sock is not None:
                sock.close()
            time.sleep(min(1.0, max(0.0, deadline - time.monotonic())))
    raise ControlError(
        f"could not connect to coordinator at {endpoint} within {timeout}s: "
        f"{last_error}"
    )


@dataclass
class CoordinatorServer:
    experiment: str
    run_id: str
    listen_host: str
    control_port: int

    def __post_init__(self) -> None:
        self.listener: socket.socket | None = None
        self.peers: dict[str, JsonPeer] = {}

    def open(self) -> None:
        listener = socket.socket(
            socket.AF_INET6 if ":" in self.listen_host else socket.AF_INET
        )
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind((self.listen_host, self.control_port))
        listener.listen(4)
        listener.settimeout(0.5)
        self.listener = listener

    def accept_roles(self, roles: Iterable[str], timeout: float) -> None:
        if self.listener is None:
            raise RuntimeError("coordinator server is not open")
        wanted = set(roles)
        deadline = time.monotonic() + timeout
        while set(self.peers) != wanted:
            if time.monotonic() >= deadline:
                missing = sorted(wanted - set(self.peers))
                raise ControlError(f"timed out waiting for roles: {missing}")
            try:
                sock, address = self.listener.accept()
            except socket.timeout:
                continue
            sock.settimeout(None)
            peer = JsonPeer(sock, f"pending-{address}")
            try:
                hello = peer.recv(
                    timeout=min(5.0, max(0.1, deadline - time.monotonic()))
                )
                reason = self._handshake_rejection(hello, wanted)
                if reason is not None:
                    peer.send({"type": "REJECT", "reason": reason})
                    peer.close()
                    continue
                role = str(hello["role"])
                peer.name = role
                self.peers[role] = peer
                peer.send({"type": "HELLO_ACK", "protocol": PROTOCOL_VERSION})
            except (ControlError, TimeoutError):
                peer.close()

    def _handshake_rejection(
        self, hello: dict[str, Any], wanted: set[str]
    ) -> str | None:
        if hello.get("type") != "HELLO":
            return "first message must be HELLO"
        if hello.get("protocol") != PROTOCOL_VERSION:
            return f"protocol must be {PROTOCOL_VERSION}"
        if hello.get("experiment") != self.experiment:
            return "experiment mismatch"
        if hello.get("run_id") != self.run_id:
            return "run_id mismatch"
        role = hello.get("role")
        if role not in wanted:
            return "unexpected role"
        if role in self.peers:
            return "duplicate role"
        return None

    def send(self, role: str, message: dict[str, Any]) -> None:
        self.peers[role].send(message)

    def broadcast(self, message: dict[str, Any]) -> None:
        for peer in self.peers.values():
            peer.send(message)

    def recv_any(self, timeout: float) -> tuple[str, dict[str, Any]]:
        deadline = time.monotonic() + timeout
        while True:
            for role, peer in self.peers.items():
                try:
                    return role, peer.recv_nowait()
                except TimeoutError:
                    pass
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError
            time.sleep(min(0.02, remaining))

    def finish(self, result: str, exit_code: int, timeout: float = 30) -> None:
        message = {"type": "FINISH", "result": result, "exit_code": exit_code}
        for role, peer in list(self.peers.items()):
            try:
                peer.send(message)
            except ControlError:
                del self.peers[role]
        pending = set(self.peers)
        deadline = time.monotonic() + timeout
        while pending and time.monotonic() < deadline:
            try:
                role, reply = self.recv_any(deadline - time.monotonic())
            except (TimeoutError, ControlError):
                break
            if reply.get("type") == "FINISH_ACK":
                pending.discard(role)

    def close(self) -> None:
        for peer in self.peers.values():
            peer.close()
        self.peers.clear()
        if self.listener is not None:
            self.listener.close()
            self.listener = None

    def __enter__(self) -> "CoordinatorServer":
        self.open()
        return self

    def __exit__(self, *_: object) -> None:
        self.close()


def command(name: str, **fields: Any) -> dict[str, Any]:
    return {"type": "COMMAND", "name": name, **fields}


def event(kind: str, **fields: Any) -> dict[str, Any]:
    return {"type": "EVENT", "kind": kind, **fields}
