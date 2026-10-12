// Package canmsg decodes the CAN frames the backend cares about. Layouts mirror can/fleetbus.dbc and are checked
// against protocol/can_vectors.json (generated from the DBC with cantools).
package canmsg

import "encoding/binary"

const (
	IDHeartbeat     = 0x100
	IDBatteryStatus = 0x200
	IDBodyStatus    = 0x201
	IDFaultEvent    = 0x300
	IDOtaCtrl       = 0x400
	IDOtaData       = 0x401
	IDOtaAck        = 0x402
	IDVehicleObd    = 0x500

	ProtoVer = 1
)

// Node ids (roles).
const (
	NodeBattery    = 1
	NodeBody       = 2
	NodeTelematics = 3
)

// States, matching VAL_ State in the DBC.
const (
	StateBoot = iota
	StateNormal
	StateWarning
	StateFault
	StateSafe
	StateLimpHome
	StateUpdating
)

var StateNames = []string{"BOOT", "NORMAL", "WARNING", "FAULT", "SAFE", "LIMP_HOME", "UPDATING"}

func StateName(s int) string {
	if s >= 0 && s < len(StateNames) {
		return StateNames[s]
	}
	return "UNKNOWN"
}

type Heartbeat struct {
	NodeID    uint8
	FwVersion uint16
	UptimeS   uint32
	State     uint8
	Slot      uint8
	Trial     bool
	ProtoVer  uint8
}

type BatteryStatus struct {
	PackMilliVolt   uint16
	CurrentMilliAmp int16
	TempDeciC       int16
	SocPct          uint8
	ProtoVer        uint8
}

type BodyStatus struct {
	Lights, ServoDeg uint8
	LimpHome         bool
	ProtoVer         uint8
}

type FaultEvent struct {
	Code     uint16
	Severity uint8
	NodeID   uint8
	ProtoVer uint8
}

type VehicleObd struct {
	SpeedKph   uint8
	RpmQuarter uint16
	CoolantC   uint8
	ProtoVer   uint8
}

func DecodeHeartbeat(d []byte) (Heartbeat, bool) {
	if len(d) != 8 {
		return Heartbeat{}, false
	}
	return Heartbeat{
		NodeID: d[0], FwVersion: binary.LittleEndian.Uint16(d[1:3]),
		UptimeS: uint32(d[3]) | uint32(d[4])<<8 | uint32(d[5])<<16,
		State:   d[6] & 0x0F, Slot: d[6] >> 4 & 1, Trial: d[6]>>5&1 == 1, ProtoVer: d[7],
	}, true
}

func DecodeBatteryStatus(d []byte) (BatteryStatus, bool) {
	if len(d) != 8 {
		return BatteryStatus{}, false
	}
	return BatteryStatus{
		PackMilliVolt: binary.LittleEndian.Uint16(d[0:2]), CurrentMilliAmp: int16(binary.LittleEndian.Uint16(d[2:4])),
		TempDeciC: int16(binary.LittleEndian.Uint16(d[4:6])), SocPct: d[6], ProtoVer: d[7],
	}, true
}

func DecodeBodyStatus(d []byte) (BodyStatus, bool) {
	if len(d) != 4 {
		return BodyStatus{}, false
	}
	return BodyStatus{Lights: d[0], ServoDeg: d[1], LimpHome: d[2] != 0, ProtoVer: d[3]}, true
}

func DecodeFaultEvent(d []byte) (FaultEvent, bool) {
	if len(d) != 8 {
		return FaultEvent{}, false
	}
	return FaultEvent{Code: binary.LittleEndian.Uint16(d[0:2]), Severity: d[2], NodeID: d[3], ProtoVer: d[7]}, true
}

func DecodeVehicleObd(d []byte) (VehicleObd, bool) {
	if len(d) != 8 {
		return VehicleObd{}, false
	}
	return VehicleObd{SpeedKph: d[0], RpmQuarter: binary.LittleEndian.Uint16(d[1:3]), CoolantC: d[3], ProtoVer: d[7]}, true
}
