#pragma once

#include <string>

// -----------------------------------------------------------------------------
// Monitor - live world view in a browser (Tools/monitor.html)
//
// The game is the server: a small HTTP listener on 127.0.0.1 that streams JSON
// messages as Server-Sent Events. SSE rather than WebSocket because the page
// side is a plain `new EventSource(url)` with automatic reconnect, and the
// server side needs no handshake hashing or frame encoding - one message is
// "data: <json>\n\n".
//
// Everything runs on the main thread inside Poll() with non-blocking sockets.
// No worker thread means the snapshot is built straight from game state with
// no locks and no torn reads.
//
// Loopback only: nothing is reachable from another machine and Windows does
// not raise a firewall prompt. This is a debug tap, not a network feature, so
// the single-player rule (CLAUDE.md 1) still holds.
//
// Routes
//   GET /events   SSE stream: hello, the latest snapshot, then every message
//   GET /state    latest snapshot as plain JSON (curl, scripts)
//   GET /         the monitor page itself, read from disk on every request
//
// Message schema "gg-monitor/1" is documented at the top of monitor.html.
//
// This header stays free of winsock / windows.h for the same reason as
// Platform.h.
// -----------------------------------------------------------------------------
namespace Monitor
{
	// Tries port, port+1 ... port+9 so a second game instance still gets one.
	// Returns false if none bind; the game runs on without the monitor.
	bool Init(int port, const char* htmlPath);
	void Shutdown();

	// Accept, read requests, flush pending output. Call once per frame.
	void Poll();

	int  GetPort();          // 0 when not running
	int  ClientCount();      // open /events streams

	// A full snapshot. Sent to every stream and kept so a page that connects
	// later (or reloads) fills in immediately instead of waiting a tick.
	void Publish(const std::string& snapshotJson);

	// Any other one-line JSON message (log lines, one-off events).
	void Send(const std::string& json);
}
