package main

import (
	"encoding/binary"
	"fmt"
	"io"
	"net"
	"os"
)

const maxFrame = 4096

// chunkWriter makes every Write deliberately short. TCP is allowed to do
// this, so the framing code must keep writing the bytes that remain.
type chunkWriter struct {
	writer io.Writer
	max    int
}

func (w chunkWriter) Write(p []byte) (int, error) {
	if len(p) > w.max {
		p = p[:w.max]
	}
	return w.writer.Write(p)
}

// chunkReader exposes at most one byte per Read. A correct reader cannot
// assume that either the header or payload arrives in one piece.
type chunkReader struct{ reader io.Reader }

func (r chunkReader) Read(p []byte) (int, error) {
	if len(p) > 1 {
		p = p[:1]
	}
	return r.reader.Read(p)
}

func writeFrame(w io.Writer, payload []byte) error {
	// TODO 1: reject payload larger than maxFrame
	// TODO 2: encode len(payload) as a four-byte big-endian header and write it completely
	// TODO 3: write every payload byte (a short write must retry the remainder)
	return nil
}

func readFrame(r io.Reader) ([]byte, error) {
	// TODO 4: read the four-byte header with io.ReadFull
	// TODO 5: decode the frame size and reject values above maxFrame
	// TODO 6: allocate the payload and read it completely with io.ReadFull
	return nil, nil
}

func main() {
	listener, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		panic(err)
	}
	defer listener.Close()

	done := make(chan error, 1)
	go func() {
		conn, err := listener.Accept()
		if err != nil {
			done <- err
			return
		}
		defer conn.Close()
		request, err := readFrame(chunkReader{reader: conn})
		if err == nil && string(request) != "PING" {
			err = fmt.Errorf("unexpected request %q", request)
		}
		if err == nil {
			err = writeFrame(chunkWriter{writer: conn, max: 2}, []byte("PONG"))
		}
		done <- err
	}()

	conn, err := net.Dial("tcp", listener.Addr().String())
	if err != nil {
		panic(err)
	}
	if err = writeFrame(chunkWriter{writer: conn, max: 2}, []byte("PING")); err == nil {
		var reply []byte
		reply, err = readFrame(chunkReader{reader: conn})
		if err == nil && string(reply) != "PONG" {
			err = fmt.Errorf("unexpected reply %q", reply)
		}
	}
	conn.Close()
	if serverErr := <-done; err == nil {
		err = serverErr
	}
	if err != nil {
		panic(err)
	}
	if err := os.WriteFile("/tmp/g00-proof", []byte("PING->PONG\n"), 0600); err != nil {
		panic(err)
	}
}

var _ = binary.BigEndian
