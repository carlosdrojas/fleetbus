// MOCK(M8): static fleet grid. Real version: fleet, node detail, OTA page, car view; data over REST + SSE.
const nodes = [
  { name: "battery", fw: "0.0.0", health: "ok" },
  { name: "body", fw: "0.0.0", health: "ok" },
];

export default function App() {
  return (
    <main>
      <h1>FleetBus</h1>
      <p>MOCK DATA, SIMULATED NODES</p>
      {nodes.map((n) => (
        <div key={n.name}>{n.name} · fw {n.fw} · {n.health}</div>
      ))}
    </main>
  );
}
