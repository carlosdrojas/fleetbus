import { useLive, NODE_LABEL, type GatewayView } from "./api";

/** Glanceable, phone-sized. Operated and filmed by a passenger; never meant to be read while driving. */
export function CarView({ gw }: { gw: number }) {
  const { data, error } = useLive<{ gateways: GatewayView[] }>("/api/fleet", 1000);
  const g = data?.gateways.find((x) => x.id === gw);
  const worst = !g ? "silent" : g.nodes.some((n) => n.health === "critical") ? "critical" : g.nodes.some((n) => n.health === "silent") ? "silent" : g.nodes.some((n) => n.health === "warning") ? "warning" : "ok";
  const headline = { ok: "All systems normal", warning: "Attention", critical: "Fault", silent: g ? "Node not reporting" : "Waiting for car" }[worst];
  return (
    <div className="car">
      <a className="muted back" href="#/">‹ Fleet</a>
      <div className={`car-status ${worst}`} role="status"><i aria-hidden /><h1>{headline}</h1><p>{g ? `Car ${g.id}${g.simulated ? " · simulated" : ""}` : error ?? "Connecting…"}</p></div>
      <div className="car-nodes">
        {(g?.nodes ?? []).map((n) => (
          <div key={n.node_id} className={`car-node ${n.health}`}>
            <span>{NODE_LABEL[n.node_id]}</span>
            <b>{n.state_name.replace("_", " ")}</b>
            <small>v{n.fw_version} · slot {n.slot_name}{n.trial ? " · trial" : ""}</small>
          </div>
        ))}
      </div>
      <p className="muted small center">Read-only: the in-car node only listens to the OBD-II port.</p>
    </div>
  );
}
