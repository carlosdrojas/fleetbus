import { useState } from "react";
import { useLive, NODE_LABEL, type MetricRow, type NodeEvent } from "./api";
import { Chart } from "./Chart";

const STATES = ["BOOT", "NORMAL", "WARNING", "FAULT", "SAFE", "LIMP HOME", "UPDATING"];
const fmt = (k: string, v: unknown) => (k === "from" || k === "to") && typeof v === "number" && v < STATES.length && STATES[v] ? STATES[v] : String(v);

const SERIES: Record<number, { name: string; label: string; unit: string; color?: string; min?: number; max?: number }[]> = {
  1: [
    { name: "pack_mv", label: "Pack voltage", unit: "mV" },
    { name: "current_ma", label: "Current", unit: "mA", color: "var(--blue)" },
    { name: "temp_c", label: "Temperature", unit: "°C", color: "var(--orange)" },
    { name: "soc_pct", label: "State of charge", unit: "%", color: "var(--accent)", min: 0, max: 100 },
  ],
  2: [
    { name: "servo_deg", label: "Servo angle", unit: "°", color: "var(--blue)", min: 0, max: 180 },
    { name: "limp_home", label: "Limp-home", unit: "", color: "var(--red)", min: 0, max: 1 },
  ],
  3: [
    { name: "speed_kph", label: "Speed", unit: "km/h", color: "var(--blue)" },
    { name: "rpm", label: "Engine", unit: "rpm", color: "var(--orange)" },
  ],
};

function Series({ gw, node, s, window }: { gw: number; node: number; s: (typeof SERIES)[number][number]; window: string }) {
  const { data } = useLive<MetricRow[]>(`/api/nodes/${gw}/${node}/metrics?name=${s.name}&window=${window}`, 2000);
  return (
    <section className="card">
      <h4>{s.label}</h4>
      <Chart rows={data ?? []} unit={s.unit} color={s.color} min={s.min} max={s.max} />
    </section>
  );
}

export function NodeDetail({ gw, node }: { gw: number; node: number }) {
  const [window, setWindow] = useState("2m");
  const { data: events } = useLive<NodeEvent[]>("/api/events");
  const mine = (events ?? []).filter((e) => e.gateway_id === gw && e.node_id === node).slice(0, 12);
  return (
    <>
      <p><a href="#/" className="muted">‹ Fleet</a></p>
      <div className="hero row">
        <h1>{NODE_LABEL[node] ?? `Node ${node}`} <span className="muted">· car {gw}</span></h1>
        <div className="seg" role="group" aria-label="Time window">
          {["1m", "2m", "10m"].map((w) => <button key={w} aria-pressed={w === window} onClick={() => setWindow(w)}>{w}</button>)}
        </div>
      </div>
      <div className="grid wide">{(SERIES[node] ?? []).map((s) => <Series key={s.name} gw={gw} node={node} s={s} window={window} />)}</div>
      <h3 className="section">Recent events</h3>
      <ul className="events">
        {mine.length === 0 && <li className="muted">Nothing yet.</li>}
        {mine.map((e) => (
          <li key={e.id}><time className="mono muted">{new Date(e.ts).toLocaleTimeString()}</time><b>{e.kind.replace("_", " ")}</b><span className="muted mono">{Object.entries(e.detail).map(([k, v]) => `${k}=${e.kind === "state" ? fmt(k, v) : String(v)}`).join("  ")}</span></li>
        ))}
      </ul>
    </>
  );
}
