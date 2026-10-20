import { createServer } from "node:http";
import { readFile } from "node:fs/promises";
import { extname, join, normalize, dirname, sep } from "node:path";
import { fileURLToPath } from "node:url";
import { Server } from "socket.io";
import { WebSocketServer } from "ws";
import { createHub } from "./hub.js";

const PORT = Number(process.env.PORT) || 6000;
const HOST = process.env.HOST || "0.0.0.0";
const TOKEN = process.env.RUMOR_TOKEN || "";
const PUBLIC = join(dirname(fileURLToPath(import.meta.url)), "..", "public");

const TYPES = {
  ".html": "text/html; charset=utf-8",
  ".css": "text/css; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".png": "image/png",
  ".jpg": "image/jpeg",
  ".jpeg": "image/jpeg",
};

function tokenOk(got) {
  return !TOKEN || got === TOKEN;
}

function serve(req, res) {
  const url = new URL(req.url, "http://localhost");
  if (url.pathname.startsWith("/socket.io")) return;
  if (url.pathname === "/health") {
    res.writeHead(200, { "content-type": "text/plain" });
    res.end("ok");
    return;
  }
  const rel = (url.pathname === "/" ? "index.html" : url.pathname.replace(/^\/+/, ""));
  if (rel.split("/").includes("..")) {
    res.writeHead(403);
    res.end();
    return;
  }
  const file = normalize(join(PUBLIC, rel));
  const root = PUBLIC.endsWith(sep) ? PUBLIC : PUBLIC + sep;
  if (!file.startsWith(root)) {
    res.writeHead(403);
    res.end();
    return;
  }
  readFile(file)
    .then((buf) => {
      res.writeHead(200, {
        "content-type": TYPES[extname(file)] || "application/octet-stream",
        "cache-control": "no-cache",
      });
      res.end(buf);
    })
    .catch(() => {
      res.writeHead(404);
      res.end("not found");
    });
}

const httpServer = createServer(serve);
const io = new Server(httpServer, { destroyUpgrade: false, cors: { origin: true } });
const hub = createHub(io);
const devices = new WebSocketServer({ noServer: true });

io.use((socket, next) => {
  if (tokenOk(socket.handshake.auth?.token || "")) next();
  else next(new Error("unauthorized"));
});

io.on("connection", (socket) => {
  hub.sync(socket);
  socket.on("cmd", (msg) => {
    if (!msg || typeof msg !== "object") return;
    const { deviceId, ...rest } = msg;
    hub.command(deviceId, rest);
  });
});

httpServer.on("upgrade", (req, socket, head) => {
  const url = new URL(req.url, "http://localhost");
  if (url.pathname !== "/device") return;
  if (!tokenOk(url.searchParams.get("token") || "")) {
    socket.write("HTTP/1.1 401 Unauthorized\r\nConnection: close\r\n\r\n");
    socket.destroy();
    return;
  }
  devices.handleUpgrade(req, socket, head, (ws) => devices.emit("connection", ws, req));
});

devices.on("connection", (ws) => {
  const device = hub.add(ws);
  ws.on("message", (data, isBinary) => {
    if (isBinary || data.length > 262144) return;
    try {
      hub.ingest(device, JSON.parse(data.toString()));
    } catch {
      /* ignore malformed frames */
    }
  });
  ws.on("close", () => hub.remove(device.id));
});

httpServer.listen(PORT, HOST, () => {
  console.log(`RUMOR remote  http://${HOST}:${PORT}`);
});
