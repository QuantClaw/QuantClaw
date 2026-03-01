// Copyright 2024 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

import { describe, it, expect, afterEach } from "vitest";
import * as net from "node:net";
import { RpcServer } from "../src/rpc-server.js";

// Create a simple TCP server that the RpcServer can connect to.
// Binds to 127.0.0.1:0 so the OS picks a free port.
function createMockParent(): {
  server: net.Server;
  port: () => number;
  getClient: () => Promise<net.Socket>;
  close: () => void;
} {
  let resolveClient: ((s: net.Socket) => void) | null = null;
  const clientPromise = new Promise<net.Socket>((resolve) => {
    resolveClient = resolve;
  });

  const server = net.createServer((socket) => {
    resolveClient?.(socket);
  });

  server.listen(0, "127.0.0.1");

  return {
    server,
    port: () => (server.address() as net.AddressInfo).port,
    getClient: () => clientPromise,
    close: () => { server.close(); },
  };
}

// Read a line from a socket.
function readLine(socket: net.Socket, timeoutMs = 5000): Promise<string> {
  return new Promise((resolve, reject) => {
    let buffer = "";
    const timer = setTimeout(() => reject(new Error("readLine timeout")), timeoutMs);

    const onData = (chunk: Buffer) => {
      buffer += chunk.toString();
      const idx = buffer.indexOf("\n");
      if (idx !== -1) {
        clearTimeout(timer);
        socket.off("data", onData);
        resolve(buffer.slice(0, idx));
      }
    };
    socket.on("data", onData);
  });
}

describe("RpcServer", () => {
  const servers: Array<{ close: () => void }> = [];

  afterEach(() => {
    for (const s of servers) {
      try { s.close(); } catch {}
    }
    servers.length = 0;
  });

  it("should connect and respond to ping", async () => {
    const parent = createMockParent();
    servers.push(parent);
    // Wait for server to be listening before getting port
    await new Promise<void>((r) => parent.server.listening ? r() : parent.server.once("listening", r));

    const rpc = new RpcServer({
      port: parent.port(),
      methods: { ping: () => ({}) },
    });

    await rpc.connect();
    const client = await parent.getClient();

    // Send ping request.
    client.write('{"jsonrpc":"2.0","method":"ping","params":{},"id":1}\n');

    const response = await readLine(client);
    const parsed = JSON.parse(response);
    expect(parsed.jsonrpc).toBe("2.0");
    expect(parsed.id).toBe(1);
    expect(parsed.result).toEqual({});

    rpc.disconnect();
  });

  it("should handle method not found", async () => {
    const parent = createMockParent();
    servers.push(parent);
    await new Promise<void>((r) => parent.server.listening ? r() : parent.server.once("listening", r));

    const rpc = new RpcServer({
      port: parent.port(),
      methods: { ping: () => ({}) },
    });

    await rpc.connect();
    const client = await parent.getClient();

    client.write('{"jsonrpc":"2.0","method":"nonexistent","params":{},"id":2}\n');

    const response = await readLine(client);
    const parsed = JSON.parse(response);
    expect(parsed.error).toBeDefined();
    expect(parsed.error.code).toBe(-32601);

    rpc.disconnect();
  });

  it("should handle parse error", async () => {
    const parent = createMockParent();
    servers.push(parent);
    await new Promise<void>((r) => parent.server.listening ? r() : parent.server.once("listening", r));

    const rpc = new RpcServer({
      port: parent.port(),
      methods: { ping: () => ({}) },
    });

    await rpc.connect();
    const client = await parent.getClient();

    client.write("not valid json\n");

    const response = await readLine(client);
    const parsed = JSON.parse(response);
    expect(parsed.error).toBeDefined();
    expect(parsed.error.code).toBe(-32700);

    rpc.disconnect();
  });

  it("should handle async method handlers", async () => {
    const parent = createMockParent();
    servers.push(parent);
    await new Promise<void>((r) => parent.server.listening ? r() : parent.server.once("listening", r));

    const rpc = new RpcServer({
      port: parent.port(),
      methods: {
        "plugin.tools": async () => [
          { name: "weather", description: "Get weather" },
        ],
      },
    });

    await rpc.connect();
    const client = await parent.getClient();

    client.write('{"jsonrpc":"2.0","method":"plugin.tools","params":{},"id":3}\n');

    const response = await readLine(client);
    const parsed = JSON.parse(response);
    expect(parsed.result).toEqual([
      { name: "weather", description: "Get weather" },
    ]);

    rpc.disconnect();
  });

  it("should handle method handler errors", async () => {
    const parent = createMockParent();
    servers.push(parent);
    await new Promise<void>((r) => parent.server.listening ? r() : parent.server.once("listening", r));

    const rpc = new RpcServer({
      port: parent.port(),
      methods: {
        failing: () => { throw new Error("intentional error"); },
      },
    });

    await rpc.connect();
    const client = await parent.getClient();

    client.write('{"jsonrpc":"2.0","method":"failing","params":{},"id":4}\n');

    const response = await readLine(client);
    const parsed = JSON.parse(response);
    expect(parsed.error).toBeDefined();
    expect(parsed.error.message).toContain("intentional error");

    rpc.disconnect();
  });

  it("should handle multiple sequential requests", async () => {
    const parent = createMockParent();
    servers.push(parent);
    await new Promise<void>((r) => parent.server.listening ? r() : parent.server.once("listening", r));

    let callCount = 0;
    const rpc = new RpcServer({
      port: parent.port(),
      methods: {
        counter: () => ({ count: ++callCount }),
      },
    });

    await rpc.connect();
    const client = await parent.getClient();

    for (let i = 1; i <= 3; i++) {
      client.write(`{"jsonrpc":"2.0","method":"counter","params":{},"id":${i}}\n`);
      const response = await readLine(client);
      const parsed = JSON.parse(response);
      expect(parsed.result.count).toBe(i);
    }

    rpc.disconnect();
  });

  it("should report connected state", async () => {
    const parent = createMockParent();
    servers.push(parent);
    await new Promise<void>((r) => parent.server.listening ? r() : parent.server.once("listening", r));

    const rpc = new RpcServer({
      port: parent.port(),
      methods: { ping: () => ({}) },
    });

    expect(rpc.isConnected()).toBe(false);
    await rpc.connect();
    expect(rpc.isConnected()).toBe(true);
    rpc.disconnect();
    expect(rpc.isConnected()).toBe(false);
  });
});
