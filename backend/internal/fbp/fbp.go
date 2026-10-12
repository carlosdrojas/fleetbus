// Package fbp is the Go side of the FleetBus Protocol v1. See protocol/spec.md.
// It must stay byte-compatible with gateway/fleetbus/fbp.py; the golden vectors enforce that.
package fbp

import (
	"encoding/binary"
	"errors"
	"hash/crc32"
)

const (
	Version        = 1
	HeaderSize     = 12
	MaxDatagram    = 1200
	MaxPayload     = MaxDatagram - HeaderSize - 4
	FlagRetransmit = 0x01
	FlagSimulated  = 0x02
	FlagsMask      = FlagRetransmit | FlagSimulated
)

const (
	TypeTelemetry = 1
	TypeHeartbeat = 2
	TypeAck       = 3
	TypeNack      = 4
	TypeOtaOffer  = 5
	TypeOtaChunk  = 6
	TypeOtaStatus = 7
)

var (
	ErrSize     = errors.New("fbp: bad size")
	ErrMagic    = errors.New("fbp: bad magic")
	ErrVersion  = errors.New("fbp: bad version")
	ErrLength   = errors.New("fbp: length mismatch")
	ErrCRC      = errors.New("fbp: bad crc")
	ErrFlags    = errors.New("fbp: reserved flags set")
	ErrType     = errors.New("fbp: unknown type")
	ErrPayload  = errors.New("fbp: malformed payload")
	ErrTooLarge = errors.New("fbp: payload too large")
	ErrField    = errors.New("fbp: bad header field")
)

var castagnoli = crc32.MakeTable(crc32.Castagnoli)

type Packet struct {
	Type    uint8
	Src     uint8
	Flags   uint8
	Seq     uint32
	Payload []byte
}

func Encode(p Packet) ([]byte, error) {
	if len(p.Payload) > MaxPayload {
		return nil, ErrTooLarge
	}
	if p.Type < TypeTelemetry || p.Type > TypeOtaStatus || p.Flags&^FlagsMask != 0 {
		return nil, ErrField
	}
	b := make([]byte, HeaderSize+len(p.Payload)+4)
	b[0], b[1], b[2], b[3], b[4], b[5] = 0xFB, 0xB5, Version, p.Type, p.Src, p.Flags
	binary.LittleEndian.PutUint32(b[6:], p.Seq)
	binary.LittleEndian.PutUint16(b[10:], uint16(len(p.Payload)))
	copy(b[HeaderSize:], p.Payload)
	binary.LittleEndian.PutUint32(b[len(b)-4:], crc32.Checksum(b[:len(b)-4], castagnoli))
	return b, nil
}

// Decode validates and parses one datagram. The returned payload aliases b.
func Decode(b []byte) (Packet, error) {
	if len(b) < HeaderSize+4 || len(b) > MaxDatagram {
		return Packet{}, ErrSize
	}
	if b[0] != 0xFB || b[1] != 0xB5 {
		return Packet{}, ErrMagic
	}
	if b[2] != Version {
		return Packet{}, ErrVersion
	}
	n := int(binary.LittleEndian.Uint16(b[10:]))
	if HeaderSize+n+4 != len(b) {
		return Packet{}, ErrLength
	}
	if n > MaxPayload {
		return Packet{}, ErrTooLarge
	}
	if binary.LittleEndian.Uint32(b[len(b)-4:]) != crc32.Checksum(b[:len(b)-4], castagnoli) {
		return Packet{}, ErrCRC
	}
	if b[5]&^FlagsMask != 0 {
		return Packet{}, ErrFlags
	}
	if b[3] < TypeTelemetry || b[3] > TypeOtaStatus {
		return Packet{}, ErrType
	}
	return Packet{Type: b[3], Src: b[4], Flags: b[5], Seq: binary.LittleEndian.Uint32(b[6:]), Payload: b[HeaderSize : HeaderSize+n]}, nil
}

func crcOf(b []byte) uint32 { return crc32.Checksum(b, castagnoli) }

// CRC32C exposes the protocol checksum for tools that patch headers in place (retransmit flag).
func CRC32C(b []byte) uint32 { return crcOf(b) }
