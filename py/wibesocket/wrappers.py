from __future__ import annotations

from . import wibesocket_wrappers as _impl  # local module

AsyncWebSocket = _impl.AsyncWebSocket
WebSocket = _impl.WebSocket
WebSocketError = Exception  # placeholder alias

__all__ = ["AsyncWebSocket", "WebSocket", "WebSocketError"]
