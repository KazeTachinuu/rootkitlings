package main

import (
	"encoding/binary"
	"fmt"
	"io"
	"net"
	"os"
)

const maxFrame = 4096

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

type chunkReader struct{ reader io.Reader }

func (r chunkReader) Read(p []byte) (int, error) {
	if len(p) > 1 {
		p = p[:1]
	}
	return r.reader.Read(p)
}

func writeFrame(w io.Writer, payload []byte) error {
	if len(payload) > maxFrame {
		return fmt.Errorf("frame too large: %d", len(payload))
	}
	header := make([]byte, 4)
	binary.BigEndian.PutUint32(header, uint32(len(payload)))
	for _, part := range [][]byte{header, payload} {
		for len(part) > 0 {
			n, err := w.Write(part)
			if err != nil {
				return err
			}
			if n == 0 {
				return io.ErrShortWrite
			}
			part = part[n:]
		}
	}
	return nil
}

func readFrame(r io.Reader) ([]byte, error) {
	header := make([]byte, 4)
	if _, err := io.ReadFull(r, header); err != nil {
		return nil, err
	}
	size := binary.BigEndian.Uint32(header)
	if size > maxFrame {
		return nil, fmt.Errorf("frame too large: %d", size)
	}
	payload := make([]byte, size)
	_, err := io.ReadFull(r, payload)
	return payload, err
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
