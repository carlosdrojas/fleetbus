package fbp

import (
	"bytes"
	"encoding/hex"
	"encoding/json"
	"math/rand"
	"os"
	"testing"
)

type vectors struct {
	CRCCheck struct{ Crc uint32 } `json:"crc32c_check"`
	Valid    []struct {
		Name       string
		Type, Src  uint8
		Seq        uint32
		Flags      uint8
		PayloadHex string `json:"payload_hex"`
		WireHex    string `json:"wire_hex"`
		Fields     map[string]any
	}
	Invalid []struct {
		Name    string
		WireHex string `json:"wire_hex"`
	}
}

func load(t testing.TB) vectors {
	b, err := os.ReadFile("../../../protocol/golden_vectors.json")
	if err != nil {
		t.Fatal(err)
	}
	var v vectors
	if err := json.Unmarshal(b, &v); err != nil {
		t.Fatal(err)
	}
	return v
}

func TestCRC32CCheckValue(t *testing.T) {
	v := load(t)
	if v.CRCCheck.Crc != 0xE3069283 {
		t.Fatalf("vector file crc check = %x", v.CRCCheck.Crc)
	}
	if c := crcOf([]byte("123456789")); c != 0xE3069283 {
		t.Fatalf("crc32c check value = %08x", c)
	}
}

func TestGoldenValid(t *testing.T) {
	for _, g := range load(t).Valid {
		t.Run(g.Name, func(t *testing.T) {
			payload, _ := hex.DecodeString(g.PayloadHex)
			wire, _ := hex.DecodeString(g.WireHex)
			got, err := Encode(Packet{Type: g.Type, Src: g.Src, Flags: g.Flags, Seq: g.Seq, Payload: payload})
			if err != nil || !bytes.Equal(got, wire) {
				t.Fatalf("encode mismatch: err=%v\n got %x\nwant %x", err, got, wire)
			}
			p, err := Decode(wire)
			if err != nil || p.Type != g.Type || p.Src != g.Src || p.Seq != g.Seq || p.Flags != g.Flags || !bytes.Equal(p.Payload, payload) {
				t.Fatalf("decode mismatch: %+v err=%v", p, err)
			}
		})
	}
}

func TestGoldenInvalid(t *testing.T) {
	for _, g := range load(t).Invalid {
		wire, _ := hex.DecodeString(g.WireHex)
		if _, err := Decode(wire); err == nil {
			t.Errorf("%s: decoded but must be rejected", g.Name)
		}
	}
}

func TestGoldenPayloadFields(t *testing.T) {
	for _, g := range load(t).Valid {
		payload, _ := hex.DecodeString(g.PayloadHex)
		switch g.Name {
		case "telemetry_three_records":
			tm, err := DecodeTelemetry(payload)
			if err != nil || len(tm.Records) != 3 || tm.BaseTsMs != uint64(g.Fields["base_ts_ms"].(float64)) || tm.Records[2].CanID != 0x80000123 {
				t.Fatalf("telemetry: %+v %v", tm, err)
			}
			re, _ := EncodeTelemetry(tm)
			if !bytes.Equal(re, payload) {
				t.Fatal("telemetry re-encode differs")
			}
		case "nack_two_gaps":
			s, err := DecodeNack(payload)
			if err != nil || len(s) != 2 || s[0] != 44 || s[1] != 47 {
				t.Fatalf("nack: %v %v", s, err)
			}
		case "gateway_heartbeat":
			h, err := DecodeHeartbeat(payload)
			if err != nil || h != (Heartbeat{3600, 12, 0, 5}) {
				t.Fatalf("heartbeat: %+v %v", h, err)
			}
		case "ota_offer":
			o, err := DecodeOtaOffer(payload)
			if err != nil || o != (OtaOffer{2, 1, 3, 20480, 77}) || !bytes.Equal(EncodeOtaOffer(o), payload) {
				t.Fatalf("offer: %+v %v", o, err)
			}
		case "ota_status_confirmed":
			s, err := DecodeOtaStatus(payload)
			if err != nil || s.Status != OtaConfirmed || !bytes.Equal(EncodeOtaStatus(s), payload) {
				t.Fatalf("status: %+v %v", s, err)
			}
		}
	}
}

func TestSingleBitFlipsDetected(t *testing.T) {
	pl, _ := EncodeTelemetry(Telemetry{BaseTsMs: 1, Records: []CanRecord{{0, 0x100, []byte{1, 2}}}})
	wire, _ := Encode(Packet{Type: TypeTelemetry, Src: 1, Seq: 5, Payload: pl})
	for i := 0; i < len(wire)*8; i++ {
		b := append([]byte(nil), wire...)
		b[i/8] ^= 1 << (i % 8)
		if _, err := Decode(b); err == nil {
			t.Fatalf("bit flip %d not detected", i)
		}
	}
}

func TestRoundTripRandom(t *testing.T) {
	r := rand.New(rand.NewSource(1))
	for i := 0; i < 500; i++ {
		pl := make([]byte, r.Intn(300))
		r.Read(pl)
		p := Packet{Type: uint8(1 + r.Intn(7)), Src: uint8(r.Intn(256)), Flags: uint8(r.Intn(2)), Seq: r.Uint32(), Payload: pl}
		w, err := Encode(p)
		if err != nil {
			t.Fatal(err)
		}
		q, err := Decode(w)
		if err != nil || q.Seq != p.Seq || q.Type != p.Type || !bytes.Equal(q.Payload, p.Payload) {
			t.Fatalf("roundtrip failed: %v", err)
		}
	}
}

func FuzzDecode(f *testing.F) {
	for _, g := range load(f).Valid {
		w, _ := hex.DecodeString(g.WireHex)
		f.Add(w)
	}
	f.Fuzz(func(t *testing.T, b []byte) {
		p, err := Decode(b)
		if err != nil {
			return
		}
		// Anything that decodes must re-encode to the identical bytes, and payload decoders must not panic.
		w, err := Encode(p)
		if err != nil || !bytes.Equal(w, b) {
			t.Fatalf("decode/encode not canonical: %x vs %x (%v)", b, w, err)
		}
		switch p.Type {
		case TypeTelemetry:
			DecodeTelemetry(p.Payload)
		case TypeHeartbeat:
			DecodeHeartbeat(p.Payload)
		case TypeAck:
			DecodeAck(p.Payload)
		case TypeNack:
			DecodeNack(p.Payload)
		case TypeOtaOffer:
			DecodeOtaOffer(p.Payload)
		case TypeOtaChunk:
			DecodeOtaChunk(p.Payload)
		case TypeOtaStatus:
			DecodeOtaStatus(p.Payload)
		}
	})
}

func FuzzTelemetryPayload(f *testing.F) {
	f.Add([]byte{})
	f.Fuzz(func(t *testing.T, b []byte) {
		tm, err := DecodeTelemetry(b)
		if err != nil {
			return
		}
		re, err := EncodeTelemetry(tm)
		if err != nil || !bytes.Equal(re, b) {
			t.Fatalf("telemetry not canonical: %x vs %x", b, re)
		}
	})
}
