// Package imagefmt parses and verifies FleetBus signed firmware images (layout in firmware/common/image.h). The API uses
// it to refuse a bad upload up front; the device verifies again on its own, this is not a trust boundary.
package imagefmt

import (
	"bytes"
	"crypto/ed25519"
	"crypto/sha512"
	"encoding/binary"
	"errors"
	"fmt"
)

const (
	Magic     = 0x4D494246
	HeaderLen = 0x200
	SignedLen = 0x60
	SlotSize  = 48 * 1024
	HwG431    = 0x4301 // STM32G431 (Nucleo-G431RB), secondary target
	HwMSPM0   = 0x3507 // TI MSPM0G3507 (LP-MSPM0G3507), primary target since 2026-10-12
)

// Boards maps every hardware id a FleetBus bootloader accepts to a board name (firmware/boards/<name>).
var Boards = map[uint16]string{HwG431: "stm32g4", HwMSPM0: "mspm0g3507"}

type Header struct {
	HwID    uint16
	Slot    uint8
	Version uint32
	Size    uint32
	Node    uint8
	SHA512  [64]byte
	Sig     [64]byte
}

func Parse(b []byte) (Header, error) {
	if len(b) < HeaderLen+1 {
		return Header{}, errors.New("image too short")
	}
	if len(b) > SlotSize {
		return Header{}, fmt.Errorf("image is %d bytes, slot holds %d", len(b), SlotSize)
	}
	if binary.LittleEndian.Uint32(b) != Magic {
		return Header{}, errors.New("bad magic")
	}
	if b[7] != 1 {
		return Header{}, errors.New("unsupported header version")
	}
	h := Header{HwID: binary.LittleEndian.Uint16(b[4:]), Slot: b[6], Version: binary.LittleEndian.Uint32(b[8:]), Size: binary.LittleEndian.Uint32(b[12:]), Node: b[0x10]}
	copy(h.SHA512[:], b[0x20:0x60])
	copy(h.Sig[:], b[0x60:0xA0])
	if _, ok := Boards[h.HwID]; !ok {
		return h, fmt.Errorf("hardware id 0x%04x is not a FleetBus board", h.HwID)
	}
	if h.Slot > 1 {
		return h, errors.New("bad slot")
	}
	if int(h.Size) != len(b)-HeaderLen {
		return h, fmt.Errorf("header says %d body bytes, file has %d", h.Size, len(b)-HeaderLen)
	}
	return h, nil
}

// Verify checks structure, the SHA-512 of the body and the Ed25519 signature over header bytes [0, 0x60).
func Verify(b []byte, pub ed25519.PublicKey) (Header, error) {
	h, err := Parse(b)
	if err != nil {
		return h, err
	}
	sum := sha512.Sum512(b[HeaderLen:])
	if !bytes.Equal(sum[:], h.SHA512[:]) {
		return h, errors.New("body hash does not match header")
	}
	if !ed25519.Verify(pub, b[:SignedLen], h.Sig[:]) {
		return h, errors.New("bad signature")
	}
	return h, nil
}

// Build assembles and signs an STM32G431 image (used by tests and tooling; production images come from tools/sign_image.py).
func Build(body []byte, node, slot uint8, version uint32, priv ed25519.PrivateKey) []byte {
	return BuildFor(HwG431, body, node, slot, version, priv)
}

// BuildFor is Build for a given board hardware id.
func BuildFor(hw uint16, body []byte, node, slot uint8, version uint32, priv ed25519.PrivateKey) []byte {
	b := make([]byte, HeaderLen, HeaderLen+len(body))
	for i := range b {
		b[i] = 0xFF
	}
	for i := 0; i < 0x20; i++ {
		b[i] = 0
	}
	binary.LittleEndian.PutUint32(b[0:], Magic)
	binary.LittleEndian.PutUint16(b[4:], hw)
	b[6], b[7] = slot, 1
	binary.LittleEndian.PutUint32(b[8:], version)
	binary.LittleEndian.PutUint32(b[12:], uint32(len(body)))
	b[0x10] = node
	sum := sha512.Sum512(body)
	copy(b[0x20:], sum[:])
	copy(b[0x60:], ed25519.Sign(priv, b[:SignedLen]))
	return append(b, body...)
}
