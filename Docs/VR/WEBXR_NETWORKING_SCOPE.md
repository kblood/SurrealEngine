# WebXR networking scope

Status: browser MVP decision, 2026-07-22

## Decision

The WebXR browser MVP supports offline single-player and local bot matches.
Multiplayer is unsupported. The launcher and release notes must state this
directly and must not offer a join flow that the engine cannot complete.

This decision is intentionally narrower than the desktop engine roadmap. It
does not prevent a later multiplayer project, and it does not treat a linked
socket API as evidence of a working game protocol.

## Evidence from the current engine

- `UNetDriver` is an empty `USubsystem` specialization.
- `USurrealNetworkDevice` exposes only configuration-property plumbing.
- `UnrealURL` does not yet parse fully qualified network travel URLs.
- `UTcpLink` socket operations after construction are stubs.
- `UUdpLink` has native bind and text-send fragments, but receive, binary
  transfer, and scripted receive-event delivery are not implemented.
- `Engine::LoadMap` and `Engine::LoginPlayer` implement local map loading and
  local player startup, not a remote client connection/replication lifecycle.
- Current WebXR weapon aiming is a deliberately temporary local VM scope.
  Pawn/view rotation is restored after classified weapon calls, so it is not a
  replicated independent-hand state.

The browser adds another transport constraint: web pages cannot use the raw
UDP/TCP model expected by the original game. Emscripten compilation alone does
not solve that mismatch.

## Requirements to reopen multiplayer scope

1. Implement and qualify native client/server networking first: URL/travel,
   connections, channels, serialization, package maps, actor replication,
   relevancy, prediction, joins, disconnect/recovery, discovery, and protocol
   compatibility.
2. Specify a browser gateway using WebSocket or WebTransport, including server
   operation, authentication, TLS/origin policy, discovery, rate limits,
   malformed-message handling, and latency/loss behavior.
3. Specify a versioned VR extension for head and hand pose/aim state. Define
   quantization, send rate, interpolation, authority, anti-cheat limits,
   fallback for flat clients, and compatibility negotiation.
4. Add deterministic protocol fixtures plus native-server, browser-client,
   relay, reconnect, packet-loss, and mixed flat/VR integration tests.
5. Complete security, privacy, operational-cost, and abuse reviews before any
   public server browser or relay is enabled.

## Release acceptance rule

A browser artifact passes this scope only when it clearly labels multiplayer
unsupported, launches local play without attempting external game sockets, and
does not claim that controller-local weapon aim is remotely authoritative.
