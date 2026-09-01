# ALTER glossary

Independent of the shipped-product glossary. Prefer [ALTER.md](./ALTER.md) Librarian wording.

## Process

| Term | Meaning |
|------|---------|
| ALTER | Independent learning path: Advisor, Librarian, Tutor, Editor, Roommate |
| L0–L3 | Learning journeys (agent behavior). Not shipped-product J-journeys |
| Roommate mode | Metaphors applied on L0–L3; not a separate journey |

## Curriculum terms

| Term | Meaning |
|------|---------|
| `nxd` | Control daemon analog; default TCP listen **4000** |
| NX port | Default TCP **4000** |
| `listen_fd` | Listening socket (the door) after `socket`/`bind`/`listen` on TCP 4000; stays open for more clients |
| `conn_fd` | Session descriptor returned by `accept`; one client’s TCP byte stream, distinct from `listen_fd` |
| UDP 4000 | Default multimedia port; negotiate; match TCP; failover to TCP if blocked |
| SSH multiplex | Port 22 path; UDP fully disabled |
| Web session analog | HTTPS **443** / `mod_ssl`; UDP only if WebRTC negotiated |
| `ECDHE-RSA-AES128-GCM-SHA256` | Default TLS 1.2 cipher for TCP control |
| Blowfish (UDP) | Symmetric cipher for media; keys via TCP; rotate in session |
| Self-hosted P2P | No third party may see session bytes or keys |
| H.264 / VP8 | Real-time video; GPU preferred, else software |
| Opus / Vorbis / Speex | Audio / fallback / microphone |
| X11 vector mode | Primitives for GUI/text; JPEG stills; H.264/VP8 for hot regions |
| `nxproxy` / `nxagent` | X11 round-trip optimization analog |
| `RemoteTerminalsLimit` | Max concurrent terminals on the host |
| `RemoteTerminalsUserLimit` | Max concurrent terminals per user |
| Dual-transport | TCP control + UDP media after TLS/auth |
| Progressive refinement | Static screen: lossy regions sharpened toward lossless text |
| `SIGPIPE` | Kernel signal on write to a closed TCP peer; default kills the process. Editor Rule 1: `SIG_IGN` via `signal` or `sigaction` |
| Rule 2 receive bounds | Honor `read()` / `recv()` return size; never copy past the allocated buffer |
| No-stealth | Editor Rule 3: tray and/or desktop notification on connect; silent undetected mode is forbidden |
| `TcpControlDaemon` | Roommate: Maître D' |
| `UdpMediaPipeline` | Roommate: sushi conveyor |
| `VectorGraphicsProxy` | Roommate: recipe book |
| `HardwareVideoEncoder` | Roommate: industrial slicer |
| `StandaloneTerminalSession` | Roommate: intercom |
