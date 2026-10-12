// Package fbp is the Go side of the FleetBus Protocol.
// MOCK(M5): header decode only. The CRC here is a placeholder (IEEE, not CRC-32C) and there is no reorder or dedup yet.
package fbp

import (
	"encoding/binary"
	"errors"
	"hash/crc32"
)

type Packet struct {
	Type    uint8
	Src     uint8
	Seq     uint32
	Payload []byte
}

func Decode(b []byte) (Packet, error) {
	const hdr = 11
	if len(b) < hdr+4 || b[0] != 0xFB || b[1] != 0x01 {
		return Packet{}, errors.New("fbp: short or bad magic")
	}
	if crc32.ChecksumIEEE(b[:len(b)-4]) != binary.LittleEndian.Uint32(b[len(b)-4:]) {
		return Packet{}, errors.New("fbp: bad crc")
	}
	n := int(binary.LittleEndian.Uint16(b[9:11]))
	if hdr+n+4 != len(b) {
		return Packet{}, errors.New("fbp: bad length")
	}
	return Packet{Type: b[3], Src: b[4], Seq: binary.LittleEndian.Uint32(b[5:9]), Payload: b[hdr : hdr+n]}, nil
}
