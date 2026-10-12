package fbp

import "encoding/binary"

type CanRecord struct {
	DtMs  uint16
	CanID uint32 // bit31 = extended
	Data  []byte
}

type Telemetry struct {
	BaseTsMs uint64
	Records  []CanRecord
}

func DecodeTelemetry(b []byte) (Telemetry, error) {
	if len(b) < 9 {
		return Telemetry{}, ErrPayload
	}
	t := Telemetry{BaseTsMs: binary.LittleEndian.Uint64(b)}
	count := int(b[8])
	off := 9
	for i := 0; i < count; i++ {
		if off+7 > len(b) {
			return Telemetry{}, ErrPayload
		}
		dlc := int(b[off+6])
		r := CanRecord{DtMs: binary.LittleEndian.Uint16(b[off:]), CanID: binary.LittleEndian.Uint32(b[off+2:])}
		off += 7
		if dlc > 8 || off+dlc > len(b) {
			return Telemetry{}, ErrPayload
		}
		r.Data = b[off : off+dlc]
		off += dlc
		t.Records = append(t.Records, r)
	}
	if off != len(b) {
		return Telemetry{}, ErrPayload
	}
	return t, nil
}

func EncodeTelemetry(t Telemetry) ([]byte, error) {
	if len(t.Records) > 255 {
		return nil, ErrPayload
	}
	b := make([]byte, 9, 9+len(t.Records)*15)
	binary.LittleEndian.PutUint64(b, t.BaseTsMs)
	b[8] = byte(len(t.Records))
	for _, r := range t.Records {
		if len(r.Data) > 8 {
			return nil, ErrPayload
		}
		var h [7]byte
		binary.LittleEndian.PutUint16(h[:], r.DtMs)
		binary.LittleEndian.PutUint32(h[2:], r.CanID)
		h[6] = byte(len(r.Data))
		b = append(append(b, h[:]...), r.Data...)
	}
	return b, nil
}

type Heartbeat struct {
	UptimeS uint32
	Unacked uint16
	Spooled uint32
	Resent  uint32
}

func DecodeHeartbeat(b []byte) (Heartbeat, error) {
	if len(b) != 14 {
		return Heartbeat{}, ErrPayload
	}
	return Heartbeat{binary.LittleEndian.Uint32(b), binary.LittleEndian.Uint16(b[4:]), binary.LittleEndian.Uint32(b[6:]), binary.LittleEndian.Uint32(b[10:])}, nil
}

func EncodeAck(cum uint32) []byte {
	b := make([]byte, 4)
	binary.LittleEndian.PutUint32(b, cum)
	return b
}

func DecodeAck(b []byte) (uint32, error) {
	if len(b) != 4 {
		return 0, ErrPayload
	}
	return binary.LittleEndian.Uint32(b), nil
}

func EncodeNack(seqs []uint32) ([]byte, error) {
	if len(seqs) > 255 {
		return nil, ErrPayload
	}
	b := make([]byte, 1+4*len(seqs))
	b[0] = byte(len(seqs))
	for i, s := range seqs {
		binary.LittleEndian.PutUint32(b[1+4*i:], s)
	}
	return b, nil
}

func DecodeNack(b []byte) ([]uint32, error) {
	if len(b) < 1 || len(b) != 1+4*int(b[0]) {
		return nil, ErrPayload
	}
	out := make([]uint32, b[0])
	for i := range out {
		out[i] = binary.LittleEndian.Uint32(b[1+4*i:])
	}
	return out, nil
}

type OtaOffer struct {
	Node, Slot    uint8
	Version, Size uint32
	ImageID       uint32
}

func EncodeOtaOffer(o OtaOffer) []byte {
	b := make([]byte, 14)
	b[0], b[1] = o.Node, o.Slot
	binary.LittleEndian.PutUint32(b[2:], o.Version)
	binary.LittleEndian.PutUint32(b[6:], o.Size)
	binary.LittleEndian.PutUint32(b[10:], o.ImageID)
	return b
}

func DecodeOtaOffer(b []byte) (OtaOffer, error) {
	if len(b) != 14 {
		return OtaOffer{}, ErrPayload
	}
	return OtaOffer{b[0], b[1], binary.LittleEndian.Uint32(b[2:]), binary.LittleEndian.Uint32(b[6:]), binary.LittleEndian.Uint32(b[10:])}, nil
}

func EncodeOtaChunk(node uint8, offset uint32, data []byte) []byte {
	b := make([]byte, 5+len(data))
	b[0] = node
	binary.LittleEndian.PutUint32(b[1:], offset)
	copy(b[5:], data)
	return b
}

func DecodeOtaChunk(b []byte) (node uint8, offset uint32, data []byte, err error) {
	if len(b) < 5 {
		return 0, 0, nil, ErrPayload
	}
	return b[0], binary.LittleEndian.Uint32(b[1:]), b[5:], nil
}

const (
	OtaAccepted = iota
	OtaProgress
	OtaTransferred
	OtaFailed
	OtaVerifyFailed
	OtaConfirmed
	OtaRolledBack
)

type OtaStatus struct {
	Node, Status uint8
	NextOffset   uint32
	Detail       uint16
}

func EncodeOtaStatus(s OtaStatus) []byte {
	b := make([]byte, 8)
	b[0], b[1] = s.Node, s.Status
	binary.LittleEndian.PutUint32(b[2:], s.NextOffset)
	binary.LittleEndian.PutUint16(b[6:], s.Detail)
	return b
}

func DecodeOtaStatus(b []byte) (OtaStatus, error) {
	if len(b) != 8 {
		return OtaStatus{}, ErrPayload
	}
	return OtaStatus{b[0], b[1], binary.LittleEndian.Uint32(b[2:]), binary.LittleEndian.Uint16(b[6:])}, nil
}
