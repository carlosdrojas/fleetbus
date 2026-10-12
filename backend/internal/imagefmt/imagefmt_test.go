package imagefmt

import (
	"crypto/ed25519"
	"encoding/hex"
	"encoding/json"
	"os"
	"testing"
)

func golden(t *testing.T) (blob []byte, pub ed25519.PublicKey) {
	b, err := os.ReadFile("../../../protocol/golden_vectors.json")
	if err != nil {
		t.Fatal(err)
	}
	var f struct {
		G struct {
			Pub  string `json:"pubkey_hex"`
			Blob string `json:"blob_hex"`
		} `json:"golden_image"`
	}
	if err := json.Unmarshal(b, &f); err != nil {
		t.Fatal(err)
	}
	blob, _ = hex.DecodeString(f.G.Blob)
	p, _ := hex.DecodeString(f.G.Pub)
	return blob, ed25519.PublicKey(p)
}

func TestGoldenImageSignedByPythonVerifiesInGo(t *testing.T) {
	blob, pub := golden(t)
	h, err := Verify(blob, pub)
	if err != nil {
		t.Fatal(err)
	}
	if h.Node != 2 || h.Slot != 1 || h.Version != 9 || h.Size != 700 {
		t.Fatalf("%+v", h)
	}
}

func TestTamperingIsRejected(t *testing.T) {
	blob, pub := golden(t)
	flip := func(i int) []byte { c := append([]byte(nil), blob...); c[i] ^= 1; return c }
	for name, b := range map[string][]byte{"body": flip(HeaderLen + 10), "version": flip(8), "hash": flip(0x20), "sig": flip(0x60), "magic": flip(0)} {
		if _, err := Verify(b, pub); err == nil {
			t.Errorf("%s tamper accepted", name)
		}
	}
	other, _, _ := ed25519.GenerateKey(nil)
	if _, err := Verify(blob, other); err == nil {
		t.Error("wrong key accepted")
	}
	if _, err := Verify(blob[:100], pub); err == nil {
		t.Error("truncated accepted")
	}
	if _, err := Verify(append(blob, 0), pub); err == nil {
		t.Error("trailing byte accepted")
	}
}
