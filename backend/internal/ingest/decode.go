package ingest

import (
	"time"

	"github.com/carlosdrojas/fleetbus/backend/internal/canmsg"
	"github.com/carlosdrojas/fleetbus/backend/internal/fbp"
	"github.com/carlosdrojas/fleetbus/backend/internal/store"
)

// item is one delivered, in-order FBP packet waiting to be committed. The payload is a private copy.
type item struct {
	src     uint8
	seq     uint32
	epoch   uint32
	ptype   uint8
	sim     bool
	addr    string
	recv    time.Time
	payload []byte
}

// buildBatch decodes a group of items into one store.Batch. Malformed payloads are counted and skipped: a packet that
// passed its CRC but has a bad payload is a gateway bug, and must not wedge the pipeline.
func buildBatch(items []item, m *Metrics) store.Batch {
	var b store.Batch
	b.Cursors = map[int16]uint32{}
	epochOf := map[uint8]uint32{}
	seenGW := map[uint8]int{}
	for _, it := range items {
		// The cursor is the highest seq of the newest session (epoch); items of an older session cannot move it.
		if e, ok := epochOf[it.src]; !ok || it.epoch > e {
			epochOf[it.src] = it.epoch
			b.Cursors[int16(it.src)] = it.seq
		} else if it.epoch == e && it.seq > b.Cursors[int16(it.src)] {
			b.Cursors[int16(it.src)] = it.seq
		}
		gw := int16(it.src)
		if i, ok := seenGW[it.src]; !ok {
			seenGW[it.src] = len(b.Gateways)
			b.Gateways = append(b.Gateways, store.GatewayUpdate{ID: gw, Addr: it.addr, TS: it.recv, Simulated: it.sim})
		} else if it.recv.After(b.Gateways[i].TS) {
			b.Gateways[i].TS = it.recv
		}
		switch it.ptype {
		case fbp.TypeHeartbeat:
			h, err := fbp.DecodeHeartbeat(it.payload)
			if err != nil {
				m.BadPayload.Inc()
				continue
			}
			g := &b.Gateways[seenGW[it.src]]
			g.HasStats, g.UptimeS, g.Unacked, g.Spooled, g.Resent = true, int64(h.UptimeS), int32(h.Unacked), int64(h.Spooled), int64(h.Resent)
		case fbp.TypeOtaStatus:
			s, err := fbp.DecodeOtaStatus(it.payload)
			if err != nil {
				m.BadPayload.Inc()
				continue
			}
			b.OtaStatuses = append(b.OtaStatuses, store.OtaStatusRow{GatewayID: gw, NodeID: int16(s.Node), Status: s.Status, NextOffset: int32(s.NextOffset), Detail: int32(s.Detail)})
		case fbp.TypeTelemetry:
			t, err := fbp.DecodeTelemetry(it.payload)
			if err != nil {
				m.BadPayload.Inc()
				continue
			}
			for _, r := range t.Records {
				ts := store.EffectiveTS(time.UnixMilli(int64(t.BaseTsMs)+int64(r.DtMs)), it.recv)
				addFrame(&b, gw, ts, r.CanID, r.Data, m)
			}
		}
	}
	return b
}

func addFrame(b *store.Batch, gw int16, ts time.Time, canID uint32, data []byte, m *Metrics) {
	id := canID &^ 0x80000000
	var node *int16
	set := func(n int16) { node = &n }
	metric := func(n int16, name string, v float64) {
		b.Metrics = append(b.Metrics, store.MetricRow{TS: ts, GatewayID: gw, NodeID: n, Name: name, Value: v})
	}
	switch id {
	case canmsg.IDHeartbeat:
		if h, ok := canmsg.DecodeHeartbeat(data); ok && h.ProtoVer == canmsg.ProtoVer {
			set(int16(h.NodeID))
			b.Heartbeats = append(b.Heartbeats, store.NodeHeartbeat{TS: ts, GatewayID: gw, NodeID: int16(h.NodeID), FwVersion: int32(h.FwVersion),
				Slot: int16(h.Slot), Trial: h.Trial, State: int16(h.State), UptimeS: int32(h.UptimeS)})
		} else {
			m.BadFrames.Inc()
		}
	case canmsg.IDBatteryStatus:
		if x, ok := canmsg.DecodeBatteryStatus(data); ok && x.ProtoVer == canmsg.ProtoVer {
			set(canmsg.NodeBattery)
			metric(canmsg.NodeBattery, "pack_mv", float64(x.PackMilliVolt))
			metric(canmsg.NodeBattery, "current_ma", float64(x.CurrentMilliAmp))
			metric(canmsg.NodeBattery, "temp_c", float64(x.TempDeciC)/10)
			metric(canmsg.NodeBattery, "soc_pct", float64(x.SocPct))
		} else {
			m.BadFrames.Inc()
		}
	case canmsg.IDBodyStatus:
		if x, ok := canmsg.DecodeBodyStatus(data); ok && x.ProtoVer == canmsg.ProtoVer {
			set(canmsg.NodeBody)
			limp := 0.0
			if x.LimpHome {
				limp = 1
			}
			metric(canmsg.NodeBody, "limp_home", limp)
			metric(canmsg.NodeBody, "servo_deg", float64(x.ServoDeg))
			metric(canmsg.NodeBody, "lights", float64(x.Lights))
		} else {
			m.BadFrames.Inc()
		}
	case canmsg.IDFaultEvent:
		if x, ok := canmsg.DecodeFaultEvent(data); ok && x.ProtoVer == canmsg.ProtoVer {
			set(int16(x.NodeID))
			b.Faults = append(b.Faults, store.FaultRow{TS: ts, GatewayID: gw, NodeID: int16(x.NodeID), Code: int32(x.Code), Severity: int16(x.Severity)})
		} else {
			m.BadFrames.Inc()
		}
	case canmsg.IDVehicleObd:
		if x, ok := canmsg.DecodeVehicleObd(data); ok && x.ProtoVer == canmsg.ProtoVer {
			set(canmsg.NodeTelematics)
			metric(canmsg.NodeTelematics, "speed_kph", float64(x.SpeedKph))
			metric(canmsg.NodeTelematics, "rpm", float64(x.RpmQuarter)/4)
			metric(canmsg.NodeTelematics, "coolant_c", float64(int(x.CoolantC)-40))
		}
	}
	b.Telemetry = append(b.Telemetry, store.TelRow{TS: ts, GatewayID: gw, NodeID: node, CanID: int32(canID), Data: append([]byte(nil), data...)})
}
