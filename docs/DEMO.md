# PeerDesk — senior walkthrough

```mermaid
flowchart LR
  Viewer[Qt_client] -->|"Asio_TLS_Protobuf_mouse_key"| Host[C++_server]
  Host -->|"H264_frames"| Viewer
```

## What to show (5 minutes)

1. **Requirements first** — `.cursor/brain/` J0–J3 are `build-ready`. Out of scope is explicit (no files, audio, clipboard, relay, Wayland).
2. **Run the host** — `peerdesk-server --synthetic` prints `listening on …` and a TLS fingerprint. That is J0.
3. **Connect** — set `PEERDESK_CA_FILE` to the host `server.crt`. Client form: IP, port, username, password. Wrong password stays on the form. That is J1.
4. **View + control** — live H.264 frames; clicks map through a letterbox to host pixels. Synthetic mode paints a clock + last input. X11 mode captures the real display. That is J2.
5. **Disconnect / reconnect** — close the session; the server process is still listening; connect again with the same login. That is J3 (the success metric).

## Defaults

| Field | Value |
|-------|--------|
| User | `jordan` |
| Password | `peerdesk` (Argon2id on disk, HMAC on the wire) |
| Port | `4473` |
| TLS | Generated host cert; client pins via `PEERDESK_CA_FILE` |

## Stack (locked)

Protobuf envelopes, FFmpeg H.264, Asio TLS 1.2+, vcpkg (or Homebrew prefixes for local builds). No JPEG wire codec and no verify-none.
