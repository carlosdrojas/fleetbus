import { useCallback, useEffect, useRef, useState } from "react";

export type NodeView = {
  gateway_id: number; node_id: number; name: string; fw_version: number; slot: number; slot_name: string; trial: boolean;
  state: number; state_name: string; age_ms: number; health: "ok" | "warning" | "critical" | "silent"; simulated: boolean;
};
export type GatewayView = { id: number; addr: string; simulated: boolean; age_ms: number; health: string; uptime_s: number; unacked: number; spooled: number; resent: number; nodes: NodeView[] };
export type Alert = { id: number; rule: string; gateway_id: number; node_id: number; severity: string; message: string; raised_at: string; cleared_at: string | null };
export type MetricRow = { ts: string; value: number };
export type Image = { id: number; node_id: number; version: number; size_a: number; notes: string; created_at: string };
export type Rollout = { id: number; image_id: number; node_id: number; version: number; state: string; canary_gateway: number | null; failure_reason: string | null; created_at: string; started_at: string | null; finished_at: string | null };
export type Step = {
  id: number; ordinal: number; gateway_id: number; node_id: number; phase: string; state: string; detail: string; prev_version: number | null;
  transfer?: { state: string; next_offset: number; size: number } | null;
};
export type NodeEvent = { id: number; ts: string; gateway_id: number; node_id: number; kind: string; detail: Record<string, unknown> };

export async function api<T>(path: string, init?: RequestInit): Promise<T> {
  const r = await fetch(path, init);
  const body = await r.json().catch(() => ({}));
  if (!r.ok) throw new Error((body as { error?: string }).error ?? r.statusText);
  return body as T;
}

/** Live data: fetch once, then refetch whenever the server says something changed (SSE), with a slow poll as a safety net. */
export function useLive<T>(path: string | null, everyMs = 5000): { data: T | null; error: string | null; reload: () => void } {
  const [data, setData] = useState<T | null>(null);
  const [error, setError] = useState<string | null>(null);
  const pending = useRef(false);
  const reload = useCallback(() => {
    if (!path || pending.current) return;
    pending.current = true;
    api<T>(path).then((d) => { setData(d); setError(null); }).catch((e) => setError(String(e.message ?? e))).finally(() => { pending.current = false; });
  }, [path]);
  useEffect(() => {
    reload();
    const t = setInterval(reload, everyMs);
    const unsub = subscribe(reload);
    return () => { clearInterval(t); unsub(); };
  }, [reload, everyMs]);
  return { data, error, reload };
}

// One shared EventSource for the whole app; subscribers are debounced to one refetch per animation frame burst.
const subs = new Set<() => void>();
let es: EventSource | null = null;
let timer: number | undefined;
function subscribe(fn: () => void) {
  subs.add(fn);
  if (!es && typeof EventSource !== "undefined") {
    es = new EventSource("/api/stream");
    es.addEventListener("change", () => {
      window.clearTimeout(timer);
      timer = window.setTimeout(() => subs.forEach((f) => f()), 120);
    });
  }
  return () => { subs.delete(fn); };
}

export const NODE_LABEL: Record<number, string> = { 1: "Battery", 2: "Body", 3: "Telematics" };
export const ago = (ms: number) => (ms < 1500 ? "live" : ms < 60000 ? `${Math.round(ms / 1000)}s ago` : `${Math.round(ms / 60000)}m ago`);
