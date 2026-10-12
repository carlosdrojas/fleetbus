import { useMemo } from "react";
import type { MetricRow } from "./api";

/** Small dependency-free line chart (SVG). Scales to its container, draws a soft area under the line. */
export function Chart({ rows, unit, color = "var(--accent)", min, max, height = 120 }: { rows: MetricRow[]; unit: string; color?: string; min?: number; max?: number; height?: number }) {
  const w = 600;
  const { path, area, lo, hi, last } = useMemo(() => {
    if (rows.length < 2) return { path: "", area: "", lo: 0, hi: 1, last: rows.at(-1)?.value };
    const t0 = new Date(rows[0].ts).getTime(), t1 = new Date(rows.at(-1)!.ts).getTime() || t0 + 1;
    const vals = rows.map((r) => r.value);
    let lo = min ?? Math.min(...vals), hi = max ?? Math.max(...vals);
    if (hi - lo < 1e-9) { hi = lo + 1; }
    const pad = (hi - lo) * 0.08; lo -= min === undefined ? pad : 0; hi += max === undefined ? pad : 0;
    const x = (t: number) => ((t - t0) / Math.max(t1 - t0, 1)) * w;
    const y = (v: number) => height - 6 - ((v - lo) / (hi - lo)) * (height - 12);
    const pts = rows.map((r) => `${x(new Date(r.ts).getTime()).toFixed(1)},${y(r.value).toFixed(1)}`);
    const path = "M" + pts.join("L");
    return { path, area: `${path}L${w},${height}L0,${height}Z`, lo, hi, last: rows.at(-1)!.value };
  }, [rows, min, max, height]);
  return (
    <div className="chart">
      <svg viewBox={`0 0 ${w} ${height}`} preserveAspectRatio="none" role="img" aria-label={`chart, latest ${last ?? "no data"} ${unit}`}>
        {path ? (<><path d={area} fill={color} opacity="0.12" /><path d={path} fill="none" stroke={color} strokeWidth="2" vectorEffect="non-scaling-stroke" strokeLinejoin="round" /></>) : null}
      </svg>
      <div className="chart-axis"><span>{path ? hi.toFixed(hi > 100 ? 0 : 1) : ""}</span><span>{path ? lo.toFixed(lo > 100 ? 0 : 1) : "no data yet"}</span></div>
      <div className="chart-last">{last === undefined ? "–" : `${last.toFixed(Math.abs(last) >= 100 ? 0 : 1)} ${unit}`}</div>
    </div>
  );
}
