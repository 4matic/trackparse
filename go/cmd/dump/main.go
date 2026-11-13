// Command dump is the Go side of the cross-port diff (scripts/compare-ports.mjs): JSONL in,
// JSON out.
//
// Each stdin line is an object {"fn", "input", "options", "formatOptions", "edits"} as in the
// shared fixtures; each stdout line is the JSON result of that call, written as the JavaScript
// package writes it. A line that is not a JSON object gives null. Non-finite numbers travel as
// 1e999 / -1e999, which the reader takes as infinity.
//
//	go run ./cmd/dump < cases.jsonl > results.jsonl
package main

import (
	"bufio"
	"fmt"
	"io"
	"os"

	_ "github.com/4matic/trackparse/go" // fills in wire.Call
	"github.com/4matic/trackparse/go/internal/wire"
)

func main() {
	if err := run(os.Stdin, os.Stdout); err != nil {
		fmt.Fprintln(os.Stderr, "dump:", err)
		os.Exit(1)
	}
}

func run(in io.Reader, out io.Writer) error {
	r := bufio.NewReader(in)
	w := bufio.NewWriter(out)
	for {
		line, err := r.ReadBytes('\n')
		if len(line) > 0 {
			if line[len(line)-1] == '\n' {
				line = line[:len(line)-1]
			}
			if len(line) > 0 && line[len(line)-1] == '\r' {
				line = line[:len(line)-1]
			}
			if _, werr := w.Write(append(wire.Call(line), '\n')); werr != nil {
				return werr
			}
		}
		if err == io.EOF {
			break
		}
		if err != nil {
			return err
		}
	}
	return w.Flush()
}
