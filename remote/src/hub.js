import { randomBytes } from "node:crypto";

export function createHub(io) {
  const devices = new Map();

  function list() {
    return [...devices.values()].map((d) => ({ id: d.id, name: d.name }));
  }

  function emitDevices() {
    io.emit("devices", list());
  }

  function sync(socket) {
    for (const d of devices.values()) {
      socket.emit("hello", { id: d.id, name: d.name, catalog: d.catalog });
      if (d.state) socket.emit("state", { id: d.id, state: d.state });
    }
    socket.emit("devices", list());
  }

  function add(ws) {
    const device = {
      id: randomBytes(4).toString("hex"),
      name: "RUMOR",
      catalog: null,
      state: null,
      ws,
    };
    devices.set(device.id, device);
    emitDevices();
    return device;
  }

  function remove(id) {
    if (!devices.delete(id)) return;
    io.emit("bye", { id });
    emitDevices();
  }

  function ingest(device, msg) {
    if (!msg || typeof msg !== "object") return;
    if (msg.t === "hello") {
      device.name = typeof msg.name === "string" && msg.name ? msg.name : "RUMOR";
      device.catalog = msg.catalog ?? null;
      io.emit("hello", { id: device.id, name: device.name, catalog: device.catalog });
      emitDevices();
      return;
    }
    if (msg.t === "state") {
      device.state = msg.state ?? null;
      io.emit("state", { id: device.id, state: device.state });
      return;
    }
    if (msg.t === "debug") {
      io.emit("debug", { id: device.id, text: String(msg.text ?? "") });
    }
  }

  function command(deviceId, msg) {
    let device = deviceId ? devices.get(deviceId) : null;
    if (!device && devices.size === 1) device = devices.values().next().value;
    if (!device || device.ws.readyState !== 1) return false;
    device.ws.send(JSON.stringify(msg));
    return true;
  }

  return { add, remove, ingest, command, sync };
}
