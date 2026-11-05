// Monitor - see Monitor.h. Compiled without the precompiled header so that
// winsock2.h is the first thing to pull in windows.h.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include <string>
#include <vector>
#include <cstdio>
#include <cstring>

#include "Monitor.h"

#pragma comment(lib, "Ws2_32.lib")

namespace
{
	static const int       PORT_TRIES   = 10;
	static const int       MAX_CLIENTS  = 8;
	static const size_t    MAX_REQUEST  = 8192;
	// A background tab that stops reading would otherwise grow its queue
	// forever. Past this it is dropped; EventSource reconnects by itself.
	static const size_t    MAX_BACKLOG  = 4u * 1024u * 1024u;
	// Proxies and some browsers drop an idle stream; a comment line keeps it.
	static const ULONGLONG PING_MS      = 15000;

	enum ClientKind
	{
		CK_REQUEST = 0,   // reading the HTTP request
		CK_STREAM,        // /events, stays open
		CK_CLOSING        // one response queued, close when sent
	};

	struct Client
	{
		SOCKET      s;
		int         kind;
		std::string in;
		std::string out;
		size_t      outPos;
	};

	SOCKET              g_Listen = INVALID_SOCKET;
	bool                g_WsaUp = false;
	int                 g_Port = 0;
	std::string         g_HtmlPath;
	std::string         g_Snapshot;
	std::vector<Client> g_Clients;
	ULONGLONG           g_LastPing = 0;

	void CloseClient(Client& c)
	{
		if (c.s != INVALID_SOCKET)
		{
			closesocket(c.s);
			c.s = INVALID_SOCKET;
		}
	}

	void Flush(Client& c)
	{
		while (c.s != INVALID_SOCKET && c.outPos < c.out.size())
		{
			int left = (int)(c.out.size() - c.outPos);
			int n = send(c.s, c.out.data() + c.outPos, left, 0);
			if (n > 0)
			{
				c.outPos += (size_t)n;
				continue;
			}
			if (n == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) return;
			CloseClient(c);
			return;
		}

		if (c.s == INVALID_SOCKET) return;
		c.out.clear();
		c.outPos = 0;
		if (c.kind == CK_CLOSING)
		{
			shutdown(c.s, SD_SEND);
			CloseClient(c);
		}
	}

	void Queue(Client& c, const std::string& data)
	{
		// Drop what was already sent before appending, so the buffer does not
		// creep while a slow reader keeps it partially full.
		if (c.outPos > 0 && c.outPos >= c.out.size() / 2)
		{
			c.out.erase(0, c.outPos);
			c.outPos = 0;
		}
		c.out += data;
	}

	void QueueEvent(Client& c, const std::string& json)
	{
		if (c.out.size() - c.outPos > MAX_BACKLOG)
		{
			printf("[Monitor] dropping a stalled client\n");
			CloseClient(c);
			return;
		}
		std::string frame;
		frame.reserve(json.size() + 8);
		frame += "data: ";
		frame += json;
		frame += "\n\n";
		Queue(c, frame);
	}

	void Respond(Client& c, const char* status, const char* type, const std::string& body)
	{
		char head[512];
		snprintf(head, sizeof(head),
			"HTTP/1.1 %s\r\n"
			"Content-Type: %s\r\n"
			"Content-Length: %u\r\n"
			"Access-Control-Allow-Origin: *\r\n"
			"Cache-Control: no-store\r\n"
			"Connection: close\r\n"
			"\r\n",
			status, type, (unsigned int)body.size());
		c.out.clear();
		c.outPos = 0;
		c.out = head;
		c.out += body;
		c.kind = CK_CLOSING;
	}

	bool ReadFile(const std::string& path, std::string* out)
	{
		FILE* f = 0;
		if (fopen_s(&f, path.c_str(), "rb") != 0 || !f) return false;
		char buf[16384];
		size_t n;
		out->clear();
		while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out->append(buf, n);
		fclose(f);
		return true;
	}

	std::string Hello()
	{
		char buf[128];
		snprintf(buf, sizeof(buf), "{\"t\":\"hello\",\"schema\":\"gg-monitor/1\",\"port\":%d}",
			g_Port);
		return buf;
	}

	void HandleRequest(Client& c)
	{
		size_t end = c.in.find("\r\n\r\n");
		if (end == std::string::npos) return;

		// "GET /path?query HTTP/1.1"
		size_t sp1 = c.in.find(' ');
		size_t sp2 = (sp1 == std::string::npos) ? sp1 : c.in.find(' ', sp1 + 1);
		if (sp1 == std::string::npos || sp2 == std::string::npos)
		{
			Respond(c, "400 Bad Request", "text/plain", "bad request\n");
			return;
		}
		std::string method = c.in.substr(0, sp1);
		std::string path = c.in.substr(sp1 + 1, sp2 - sp1 - 1);
		size_t q = path.find('?');
		if (q != std::string::npos) path.erase(q);
		c.in.clear();

		if (method != "GET")
		{
			Respond(c, "405 Method Not Allowed", "text/plain", "GET only\n");
			return;
		}

		if (path == "/events")
		{
			c.kind = CK_STREAM;
			c.out =
				"HTTP/1.1 200 OK\r\n"
				"Content-Type: text/event-stream; charset=utf-8\r\n"
				"Cache-Control: no-store\r\n"
				"Connection: keep-alive\r\n"
				"Access-Control-Allow-Origin: *\r\n"
				"\r\n"
				"retry: 1000\n\n";
			c.outPos = 0;
			QueueEvent(c, Hello());
			if (!g_Snapshot.empty()) QueueEvent(c, g_Snapshot);
			printf("[Monitor] page connected (%d open)\n", Monitor::ClientCount());
			return;
		}

		if (path == "/state")
		{
			Respond(c, "200 OK", "application/json; charset=utf-8",
				g_Snapshot.empty() ? std::string("{\"t\":\"none\"}") : g_Snapshot);
			return;
		}

		if (path == "/" || path == "/monitor.html")
		{
			std::string html;
			if (ReadFile(g_HtmlPath, &html))
				Respond(c, "200 OK", "text/html; charset=utf-8", html);
			else
				Respond(c, "404 Not Found", "text/plain; charset=utf-8",
					"monitor page not found at " + g_HtmlPath +
					"\nOpen Tools/monitor.html directly in the browser instead.\n");
			return;
		}

		Respond(c, "404 Not Found", "text/plain", "not found\n");
	}

	void Read(Client& c)
	{
		char buf[2048];
		for (;;)
		{
			int n = recv(c.s, buf, sizeof(buf), 0);
			if (n > 0)
			{
				// A stream never sends anything we need; read only to notice
				// the close.
				if (c.kind != CK_REQUEST) continue;
				c.in.append(buf, (size_t)n);
				if (c.in.size() > MAX_REQUEST)
				{
					CloseClient(c);
					return;
				}
				continue;
			}
			if (n == 0)
			{
				if (c.kind == CK_STREAM) printf("[Monitor] page disconnected\n");
				CloseClient(c);
				return;
			}
			if (WSAGetLastError() != WSAEWOULDBLOCK) CloseClient(c);
			return;
		}
	}
}

namespace Monitor
{
	bool Init(int port, const char* htmlPath)
	{
		g_HtmlPath = htmlPath ? htmlPath : "";

		WSADATA wsa;
		if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
		{
			printf("[Monitor] WSAStartup failed; monitor disabled\n");
			return false;
		}
		g_WsaUp = true;

		for (int i = 0; i < PORT_TRIES; ++i)
		{
			SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if (s == INVALID_SOCKET) break;

			// Without this a second game instance could bind the same port
			// and steal half the connections.
			BOOL excl = TRUE;
			setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&excl, sizeof(excl));

			sockaddr_in addr;
			memset(&addr, 0, sizeof(addr));
			addr.sin_family = AF_INET;
			addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			addr.sin_port = htons((u_short)(port + i));

			if (bind(s, (sockaddr*)&addr, sizeof(addr)) == 0 && listen(s, 8) == 0)
			{
				u_long nb = 1;
				ioctlsocket(s, FIONBIO, &nb);
				g_Listen = s;
				g_Port = port + i;
				break;
			}
			closesocket(s);
		}

		if (g_Listen == INVALID_SOCKET)
		{
			printf("[Monitor] no free port in %d..%d; monitor disabled\n",
				port, port + PORT_TRIES - 1);
			return false;
		}

		g_LastPing = GetTickCount64();
		printf("[Monitor] listening on http://127.0.0.1:%d/  (or open %s)\n",
			g_Port, g_HtmlPath.c_str());
		return true;
	}

	void Shutdown()
	{
		for (size_t i = 0; i < g_Clients.size(); ++i) CloseClient(g_Clients[i]);
		g_Clients.clear();
		if (g_Listen != INVALID_SOCKET)
		{
			closesocket(g_Listen);
			g_Listen = INVALID_SOCKET;
		}
		if (g_WsaUp)
		{
			WSACleanup();
			g_WsaUp = false;
		}
		g_Port = 0;
	}

	void Poll()
	{
		if (g_Listen == INVALID_SOCKET) return;

		for (;;)
		{
			SOCKET s = accept(g_Listen, 0, 0);
			if (s == INVALID_SOCKET) break;   // WSAEWOULDBLOCK: nobody waiting
			if ((int)g_Clients.size() >= MAX_CLIENTS)
			{
				closesocket(s);
				continue;
			}
			u_long nb = 1;
			ioctlsocket(s, FIONBIO, &nb);
			BOOL noDelay = TRUE;
			setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&noDelay, sizeof(noDelay));

			Client c;
			c.s = s;
			c.kind = CK_REQUEST;
			c.outPos = 0;
			g_Clients.push_back(c);
		}

		ULONGLONG now = GetTickCount64();
		bool ping = (now - g_LastPing >= PING_MS);
		if (ping) g_LastPing = now;

		for (size_t i = 0; i < g_Clients.size(); ++i)
		{
			Client& c = g_Clients[i];
			Read(c);
			if (c.s == INVALID_SOCKET) continue;
			if (c.kind == CK_REQUEST) HandleRequest(c);
			if (ping && c.kind == CK_STREAM) Queue(c, ": ping\n\n");
			Flush(c);
		}

		for (size_t i = 0; i < g_Clients.size(); )
		{
			if (g_Clients[i].s == INVALID_SOCKET)
			{
				g_Clients[i] = g_Clients.back();
				g_Clients.pop_back();
			}
			else ++i;
		}
	}

	int GetPort()
	{
		return g_Port;
	}

	int ClientCount()
	{
		int n = 0;
		for (size_t i = 0; i < g_Clients.size(); ++i)
			if (g_Clients[i].s != INVALID_SOCKET && g_Clients[i].kind == CK_STREAM) ++n;
		return n;
	}

	void Publish(const std::string& snapshotJson)
	{
		g_Snapshot = snapshotJson;
		Send(snapshotJson);
	}

	void Send(const std::string& json)
	{
		for (size_t i = 0; i < g_Clients.size(); ++i)
		{
			Client& c = g_Clients[i];
			if (c.s == INVALID_SOCKET || c.kind != CK_STREAM) continue;
			QueueEvent(c, json);
			Flush(c);
		}
	}
}
