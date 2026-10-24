# L19: File Transfer

## Mission

Add DOWNLOAD (chunked binary streaming) and UPLOAD (base64 JSON) handlers for
file transfer over the encrypted WebSocket channel.

L18 gives you remote shell execution, but shell output is text.
You need to move binary files: exfiltrate a database, upload a tool, grab SSH
keys.

## What you will observe

- A download streams the file as encrypted binary chunks, with a metadata header
  containing the file size
- An upload receives base64 data in the command payload, decodes it, and writes
  it to disk
- Both directions travel through the same encrypted WebSocket connection as
  every other command

## Workspace

```text
exercises/L19-file-transfer
├── go.mod
├── go.sum
└── main.go
```

Only `main.go` is editable.
The crypto, transport, WebSocket session loop, dispatch, and all previous
handlers (including SHELL from L18) are provided.

## DOWNLOAD: chunked binary streaming

The operator sends a command with Op "DOWNLOAD" and Arg set to the file path.
Unlike regular commands that go through `execute()`, DOWNLOAD is handled
directly in the session loop because it needs to send multiple messages
(metadata + chunks + done marker).

The protocol:

1. Send a metadata JSON response with the file size
2. Stream the file in 1MB chunks, each prefixed with byte 0x01
3. Send a single 0x02 byte as the done marker

Each chunk and the done marker are encrypted individually with `wsSendRaw`,
which takes raw bytes (not JSON).
The metadata response uses `wsSend`, which marshals JSON.

```text
  Operator                        Agent
    |                                |
    |--- encrypted command --------->|
    |    cmd: DOWNLOAD /etc/pass     |
    |                                |  f = os.Open(path)
    |<-- encrypted JSON ------------|  metadata
    |    {"data":{"size":1247}}      |
    |                                |
    |<-- encrypted binary [0x01|..] -|  chunk 1
    |<-- encrypted binary [0x01|..] -|  chunk 2
    |<-- encrypted binary [0x02] ----|  done
```

If the file does not exist, send an error in the metadata response and return
nil (not an error that breaks the session):

```json
{ "data": { "error": "open /no/such/file: no such file or directory" } }
```

The chunk buffer is allocated once (1 + dlChunkSize bytes), with byte 0 set to
0x01.
Each `f.Read` fills from byte 1 onward, and `wsSendRaw` sends `buf[:1+n]` where
n is the bytes read.

## UPLOAD

The operator sends a command with Op "UPLOAD" and the file payload in the
command's Data field as JSON:

```json
{ "path": "/tmp/payload", "data": "aGVsbG8gd29ybGQ=" }
```

The handler parses the Data field, decodes the base64 content, and writes it to
the specified path.
It returns "OK" on success.

Note that UPLOAD uses `cmd.Data` (a json.RawMessage) rather than `cmd.Arg`.
This is because the upload payload contains structured data (path and file
contents) that belongs in a JSON object, not a single string argument.

```text
  Operator                        Agent
    |                                |
    |--- encrypted command --------->|
    |    cmd: UPLOAD                 |
    |    data: {"path":"/tmp/x",     |
    |           "data":"aGVs..."}    |
    |                                |  parse JSON from cmd.Data
    |                                |  raw = base64.Decode(data)
    |                                |  os.WriteFile(path, raw)
    |<-- encrypted result -----------|
    |    "OK"                        |
```

## TODOs

```text
TODO 1  main.go  implement streamDownload
TODO 2  main.go  implement handleUploadCmd
```

### TODO 1: Download handler (chunked binary)

1. Open the file with `os.Open`
2. Get the file size with `f.Stat()`
3. Send metadata:
   `wsSend(map[string]any{"data": map[string]any{"size": info.Size()}})`
4. Allocate a buffer of `1 + dlChunkSize` bytes, set `buf[0] = 0x01`
5. Loop: read into `buf[1:]`, send `buf[:1+n]` via `wsSendRaw`
6. After the loop, send `[]byte{0x02}` via `wsSendRaw`

On error opening or stating the file, send the error via wsSend and return nil.

### TODO 2: Upload handler

1. Parse `cmd.Data` as JSON containing "path" and "data" fields
2. Decode the base64 "data" field
3. Write the bytes to "path" with `os.WriteFile`
4. Return "OK"

The handler signature takes the full `Command` struct (not just arg) so it can
access `cmd.Data`.

## Test it

`rklings serve L19` starts this exercise's live transfer panel at
`http://127.0.0.1:8080`.
Sizes and downloaded bytes come from the guest agent.

```console
$ ./rklings serve L19
```

In another host terminal:

```console
$ ./rklings run L19
```

Open `http://127.0.0.1:8080`.
Use the download control with `/etc/hostname` and verify the returned byte count
and content.

For upload, choose a local file, set its guest destination to `/tmp/marker`, and
submit it.
Use the shell control to read the uploaded file back from the guest.

```text
server> DOWNLOAD /etc/hostname
[streaming 7 bytes in 1 chunk]
server> UPLOAD /tmp/marker (11 bytes)
[agent: "OK"]
server> SHELL cat /tmp/marker
agent:  {"stdout":"hello world","exit_code":0}
```

Stop `run` and `serve` with `Ctrl+C` when finished.

## Validate

```console
$ ./rklings validate L19
  [+] DOWNLOAD /etc/hostname streams file contents
  [+] UPLOAD writes file with correct contents
  [+] DOWNLOAD sends correct size in metadata
  [+] PING and SHELL still work alongside file transfer
  PASS
```

## Next

The protocol now supports commands, execution, and file transfer.
M02 brings those operations together in the terminal and browser operator so you
can control a live target from one place.
