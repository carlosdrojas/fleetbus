import { useState } from "react";
import { api, useLive, NODE_LABEL, type GatewayView, type Image, type Rollout, type Step } from "./api";
import { Pill } from "./Fleet";

const STEP_HEALTH: Record<string, string> = { healthy: "ok", transferring: "warning", verifying: "warning", pending: "silent", skipped: "silent", failed: "critical", rolled_back: "critical" };
const ROLLOUT_HEALTH: Record<string, string> = { succeeded: "ok", running: "warning", failed: "critical", aborted: "silent", pending: "warning" };

function Upload({ onDone }: { onDone: () => void }) {
  const [msg, setMsg] = useState<{ ok: boolean; text: string } | null>(null);
  const [busy, setBusy] = useState(false);
  async function submit(e: React.FormEvent<HTMLFormElement>) {
    e.preventDefault();
    setBusy(true);
    try {
      const out = await api<{ id: number; node_id: number; version: number; verified: boolean }>("/api/images", { method: "POST", body: new FormData(e.currentTarget) });
      setMsg({ ok: true, text: `Stored image ${out.id}: ${NODE_LABEL[out.node_id]} v${out.version}${out.verified ? " (signature verified)" : ""}` });
      onDone();
    } catch (err) { setMsg({ ok: false, text: (err as Error).message }); }
    setBusy(false);
  }
  return (
    <form className="card upload" onSubmit={submit}>
      <h4>Upload a signed firmware build</h4>
      <p className="muted">Two files from <code>tools/sign_image.py</code>: the same version linked for slot A and for slot B. The server checks structure and signature before storing.</p>
      <label>Slot A image<input name="slot_a" type="file" required /></label>
      <label>Slot B image<input name="slot_b" type="file" required /></label>
      <label>Notes<input name="notes" type="text" placeholder="what changed" /></label>
      <button className="primary" disabled={busy}>{busy ? "Checking…" : "Upload"}</button>
      {msg && <p className={`banner ${msg.ok ? "good" : "bad"}`} role="status">{msg.text}</p>}
    </form>
  );
}

function StartRollout({ image, onStarted }: { image: Image; onStarted: (id: number) => void }) {
  const { data: fleet } = useLive<{ gateways: GatewayView[] }>("/api/fleet", 3000);
  const [canary, setCanary] = useState("");
  const [err, setErr] = useState<string | null>(null);
  const cars = (fleet?.gateways ?? []).filter((g) => g.nodes.some((n) => n.node_id === image.node_id));
  async function start() {
    setErr(null);
    try {
      const out = await api<{ id: number }>("/api/rollouts", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ image_id: image.id, canary_gateway: canary ? Number(canary) : undefined }) });
      onStarted(out.id);
    } catch (e) { setErr((e as Error).message); }
  }
  return (
    <div className="row start">
      <select value={canary} onChange={(e) => setCanary(e.target.value)} aria-label="Canary car">
        <option value="">Canary: first online car</option>
        {cars.map((g) => <option key={g.id} value={g.id}>Canary: car {g.id}</option>)}
      </select>
      <button className="primary" onClick={start}>Start rollout</button>
      {err && <span className="bad small">{err}</span>}
    </div>
  );
}

function Timeline({ id }: { id: number }) {
  const { data, reload } = useLive<{ rollout: Rollout; steps: Step[] }>(`/api/rollouts/${id}`, 1000);
  if (!data) return null;
  const r = data.rollout;
  const active = r.state === "running" || r.state === "pending";
  return (
    <section className="card" aria-live="polite">
      <div className="card-head">
        <h3>Rollout {r.id} <span className="muted">· {NODE_LABEL[r.node_id]} v{r.version}</span></h3>
        <Pill health={ROLLOUT_HEALTH[r.state] ?? "silent"}>{r.state.toUpperCase()}</Pill>
        {active && <button className="danger right" onClick={() => api(`/api/rollouts/${r.id}/abort`, { method: "POST" }).then(reload)}>Abort</button>}
      </div>
      {r.failure_reason && <p className="banner bad">Halted: {r.failure_reason}</p>}
      <ol className="steps">
        {data.steps.map((s) => {
          const t = s.transfer;
          const pct = t && t.size ? Math.min(100, Math.round((t.next_offset * 100) / t.size)) : s.state === "healthy" ? 100 : 0;
          return (
            <li key={s.id} className={`step ${s.state}`}>
              <div className="step-head">
                <b>Car {s.gateway_id}</b><span className="badge">{s.phase}</span>
                <Pill health={STEP_HEALTH[s.state] ?? "silent"}>{s.state.replace("_", " ")}</Pill>
              </div>
              <div className="bar-track" aria-hidden><div className="bar-fill" style={{ width: `${pct}%` }} /></div>
              <p className="muted small">{s.detail}</p>
            </li>
          );
        })}
      </ol>
    </section>
  );
}

export function Ota() {
  const { data: images, reload } = useLive<Image[]>("/api/images", 4000);
  const { data: rollouts, reload: reloadR } = useLive<Rollout[]>("/api/rollouts", 2000);
  const [sel, setSel] = useState<number | null>(null);
  const current = sel ?? rollouts?.[0]?.id ?? null;
  return (
    <>
      <div className="hero"><h1>Updates</h1><p className="muted">Signed images roll out canary-first. A node must come back healthy before the next one is touched; anything uncertain stops the rollout.</p></div>
      {current && <Timeline id={current} />}
      <div className="grid wide">
        <section className="card">
          <h4>Firmware images</h4>
          {(images ?? []).length === 0 && <p className="muted">No images uploaded yet.</p>}
          <ul className="list">
            {(images ?? []).map((i) => (
              <li key={i.id}>
                <div className="row"><b>{NODE_LABEL[i.node_id]} v{i.version}</b><span className="muted mono small">{(i.size_a / 1024).toFixed(1)} KB</span><span className="muted small">{i.notes}</span></div>
                <StartRollout image={i} onStarted={(id) => { setSel(id); reloadR(); }} />
              </li>
            ))}
          </ul>
        </section>
        <Upload onDone={reload} />
      </div>
      <h3 className="section">History</h3>
      <ul className="list">
        {(rollouts ?? []).map((r) => (
          <li key={r.id}><button className="linklike" onClick={() => setSel(r.id)}>Rollout {r.id} · {NODE_LABEL[r.node_id]} v{r.version}</button><Pill health={ROLLOUT_HEALTH[r.state] ?? "silent"}>{r.state}</Pill><span className="muted small">{r.failure_reason ?? ""}</span></li>
        ))}
      </ul>
    </>
  );
}
