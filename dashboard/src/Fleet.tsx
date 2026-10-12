import { useLive, ago, NODE_LABEL, type Alert, type GatewayView, type NodeView } from "./api";

export function Pill({ health, children }: { health: string; children: React.ReactNode }) {
  return <span className={`pill ${health}`}><i aria-hidden />{children}</span>;
}

function NodeTile({ n }: { n: NodeView }) {
  return (
    <a className={`tile ${n.health}`} href={`#/node/${n.gateway_id}/${n.node_id}`}>
      <div className="tile-top">
        <strong>{NODE_LABEL[n.node_id] ?? n.name}</strong>
        <Pill health={n.health}>{n.health === "silent" ? "SILENT" : n.state_name.replace("_", " ")}</Pill>
      </div>
      <div className="tile-meta">
        <span className="mono">v{n.fw_version}</span>
        <span>slot {n.slot_name}</span>
        {n.trial && <span className="badge warn" title="Booted a new image; waiting for it to prove healthy">TRIAL</span>}
        <span className="muted right">{ago(n.age_ms)}</span>
      </div>
    </a>
  );
}

function Car({ g }: { g: GatewayView }) {
  return (
    <section className={`card ${g.health === "silent" ? "dim" : ""}`} aria-label={`Car ${g.id}`}>
      <div className="card-head">
        <h3>Car {g.id}</h3>
        {g.simulated && <span className="badge sim" title="Telemetry from the host simulator running the real firmware logic">SIMULATED</span>}
        {g.health === "silent" && <Pill health="silent">GATEWAY SILENT</Pill>}
        <a className="muted right" href={`#/car/${g.id}`}>car view ›</a>
      </div>
      <div className="tiles">{g.nodes.map((n) => <NodeTile key={n.node_id} n={{ ...n, simulated: g.simulated }} />)}</div>
      <div className="foot muted mono">link: {g.unacked} unacked · {g.spooled} spooled · {g.resent} resent · up {g.uptime_s}s</div>
    </section>
  );
}

function AlertRow({ a }: { a: Alert }) {
  return (
    <li className={`alert ${a.severity}`}>
      <span className="sev" aria-hidden />
      <div><strong>{a.rule.replace(/_/g, " ")}</strong><span className="muted"> · {a.message}</span></div>
      <time className="muted mono">{new Date(a.raised_at).toLocaleTimeString()}</time>
    </li>
  );
}

export function Fleet() {
  const { data, error } = useLive<{ gateways: GatewayView[] }>("/api/fleet", 3000);
  const { data: alerts } = useLive<Alert[]>("/api/alerts");
  const gws = data?.gateways ?? [];
  const nodes = gws.flatMap((g) => g.nodes);
  const ok = nodes.filter((n) => n.health === "ok").length;
  return (
    <>
      <div className="hero">
        <h1>Fleet</h1>
        <div className="stats">
          <div><b>{gws.length}</b><span>cars</span></div>
          <div><b className={ok === nodes.length ? "good" : "bad"}>{ok}/{nodes.length}</b><span>nodes healthy</span></div>
          <div><b className={alerts?.length ? "bad" : "good"}>{alerts?.length ?? 0}</b><span>open alerts</span></div>
        </div>
      </div>
      {error && <p className="banner bad">Can't reach the API: {error}</p>}
      {alerts && alerts.length > 0 && <ul className="alerts" aria-label="Open alerts">{alerts.map((a) => <AlertRow key={a.id} a={a} />)}</ul>}
      {!data && !error && <p className="muted">Connecting…</p>}
      {data && gws.length === 0 && <p className="muted">No gateways have reported yet. Start one with <code>python -m fleetbus.gateway --sim</code>.</p>}
      <div className="grid">{gws.map((g) => <Car key={g.id} g={g} />)}</div>
    </>
  );
}
