# TestRunner2 – SerialCommunicator Remote Runner

TestRunner2 embeds the legacy `TestRunner` orchestration logic inside a TestRunner3-style remote control pipeline. It accepts a SerialCommunicator test plan from a client, runs all server/client SerialCommunicator pairs on the control server, and streams structured JSON summaries back to the operator.

## Build

```powershell
cd TestRunner2
cmake -S . -B build
cmake --build build --config Release
```

The binary lives at `TestRunner2/build/Release/TestRunner2.exe` (or `Debug` when built in debug mode).

## Command-Line Overview

### Server

The server runs on the machine that hosts the physical/virtual COM ports. It launches SerialCommunicator server/client processes locally.

```powershell
.\TestRunner2.exe --mode server ^
    --control-port 9001 ^
    --serial-exe "D:\tools\SerialCommunicator.exe"
```

Options:

| Option | Description | Default |
| --- | --- | --- |
| `--control-port` | TCP port for control messages | `9001` |
| `--serial-exe` | Absolute or relative path to `SerialCommunicator.exe` | `SerialCommunicator.exe` |

### Client

The client runs on the operator machine and sends the SerialCommunicator test plan to the server.

```powershell
.\TestRunner2.exe --mode client ^
    --server 192.168.1.50 ^
    --control-port 9001 ^
    --comports "COM3,COM4,COM5,COM6" ^
    --repetitions 5 ^
    --datasize 1024 ^
    --num-packets 100 ^
    --baudrate 115200
```

Client options:

| Option | Description | Default |
| --- | --- | --- |
| `--server` | Server IP (required) | – |
| `--control-port` | TCP control port | `9001` |
| `--comports` | Comma-separated list of COM ports in server/client pairs (`COM3,COM4,COM5,COM6` → `(COM3,COM4)` & `(COM5,COM6)`) | – |
| `--repetitions` | Number of SerialCommunicator iterations | `1` |
| `--datasize` | Payload bytes per packet | `1024` |
| `--num-packets` | Packets per iteration | `100` |
| `--baudrate` | Serial baud rate | `115200` |
| `--save-logs` | Whether SerialCommunicator should save logs (`true`/`false`) | `false` |
| `--serial-exe` | Client-side preferred SerialCommunicator path (server fallback still applies) | `SerialCommunicator.exe` |

## Workflow

1. Client sends `CONFIG_REQUEST` (JSON, length-prefixed) with the SerialCommunicator test plan.
2. Server validates COM port pairs, acknowledges with `SERVER_READY`, and starts executing the plan.
3. Once the background run finishes, the server pushes a `TEST_COMPLETE` message (success flag + final status text). The client waits for this notification and only then issues `RESULTS_REQUEST`.
4. Server returns `RESULTS_RESPONSE` containing every run and port-pair summary, and the client prints TestRunner-style summaries plus stores each run in `TestRunner2_run_<n>.json`.

## Output & Validation

- Each run prints a PASS/FAIL table for both SerialCommunicator roles (server/client) and every COM pair.
- Validation replicates the original TestRunner logic: expected bytes/packets must match `(datasize + 6) * numPackets` and `numPackets`, and error counters must be zero.
- Run reports contain the full JSON payload returned by the server, enabling downstream automation or trend analysis.

## Notes

- SerialCommunicator must be accessible on the server machine, and every COM pair must already be wired/looped (e.g., via `com0com`).
- `numPackets == 0` (infinite mode) is rejected to avoid unbounded test time.
- The server executes all SerialCommunicator processes locally, so ensure it has permission to spawn child processes and access the COM ports.

