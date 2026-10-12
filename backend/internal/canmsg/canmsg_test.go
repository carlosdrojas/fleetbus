package canmsg

import (
	"encoding/hex"
	"encoding/json"
	"os"
	"testing"
)

type vec struct {
	Message string
	ID      int
	DataHex string `json:"data_hex"`
	Signals map[string]float64
}

func TestDecodersAgreeWithDBCVectors(t *testing.T) {
	b, err := os.ReadFile("../../../protocol/can_vectors.json")
	if err != nil {
		t.Fatal(err)
	}
	var f struct{ Vectors []vec }
	if err := json.Unmarshal(b, &f); err != nil {
		t.Fatal(err)
	}
	n := 0
	for _, v := range f.Vectors {
		d, _ := hex.DecodeString(v.DataHex)
		s := v.Signals
		switch v.Message {
		case "Heartbeat":
			h, ok := DecodeHeartbeat(d)
			if !ok || float64(h.NodeID) != s["NodeId"] || float64(h.FwVersion) != s["FwVersion"] || float64(h.UptimeS) != s["UptimeS"] ||
				float64(h.State) != s["State"] || float64(h.Slot) != s["Slot"] || h.Trial != (s["Trial"] == 1) || float64(h.ProtoVer) != s["ProtoVer"] {
				t.Errorf("heartbeat %+v vs %v", h, s)
			}
		case "BatteryStatus":
			x, ok := DecodeBatteryStatus(d)
			if !ok || float64(x.PackMilliVolt) != s["PackMilliVolt"] || float64(x.CurrentMilliAmp) != s["CurrentMilliAmp"] || float64(x.TempDeciC) != s["TempDeciC"] || float64(x.SocPct) != s["SocPct"] {
				t.Errorf("battery %+v vs %v", x, s)
			}
		case "BodyStatus":
			x, ok := DecodeBodyStatus(d)
			if !ok || float64(x.Lights) != s["Lights"] || float64(x.ServoDeg) != s["ServoDeg"] || x.LimpHome != (s["LimpHome"] == 1) {
				t.Errorf("body %+v vs %v", x, s)
			}
		case "FaultEvent":
			x, ok := DecodeFaultEvent(d)
			if !ok || float64(x.Code) != s["FaultCode"] || float64(x.Severity) != s["Severity"] || float64(x.NodeID) != s["NodeId"] {
				t.Errorf("fault %+v vs %v", x, s)
			}
		case "VehicleObd":
			x, ok := DecodeVehicleObd(d)
			if !ok || float64(x.SpeedKph) != s["SpeedKph"] || float64(x.RpmQuarter) != s["RpmQuarter"] || float64(x.CoolantC) != s["CoolantC"] {
				t.Errorf("obd %+v vs %v", x, s)
			}
		default:
			continue
		}
		n++
	}
	if n < 8 {
		t.Fatalf("only %d vectors checked", n)
	}
}

func TestRejectsWrongLengths(t *testing.T) {
	if _, ok := DecodeHeartbeat(make([]byte, 7)); ok {
		t.Fatal("short heartbeat accepted")
	}
	if _, ok := DecodeBodyStatus(make([]byte, 8)); ok {
		t.Fatal("body status must be exactly 4 bytes")
	}
}
