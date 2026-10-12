import { useEffect, useState } from "react";
import { useLive, type Alert } from "./api";
import { Fleet } from "./Fleet";
import { NodeDetail } from "./NodeDetail";
import { Ota } from "./Ota";
import { CarView } from "./CarView";

function useRoute() {
  const [hash, setHash] = useState(location.hash || "#/");
  useEffect(() => { const f = () => setHash(location.hash || "#/"); addEventListener("hashchange", f); return () => removeEventListener("hashchange", f); }, []);
  return hash.replace(/^#/, "").split("/").filter(Boolean);
}

export function App() {
  const route = useRoute();
  const [section, a, b] = route;
  const { data: alerts } = useLive<Alert[]>("/api/alerts");
  const open = alerts?.length ?? 0;
  if (section === "car") return <CarView gw={Number(a) || 1} />;
  return (
    <>
      <header className="bar">
        <a className="brand" href="#/"><span className="logo" aria-hidden />FleetBus</a>
        <nav aria-label="Sections">
          <a href="#/" aria-current={!section || section === "node" ? "page" : undefined}>Fleet{open > 0 && <span className="count" aria-label={`${open} open alerts`}>{open}</span>}</a>
          <a href="#/ota" aria-current={section === "ota" ? "page" : undefined}>Updates</a>
          <a href="#/car/1" title="Phone-sized glanceable view">Car view</a>
        </nav>
      </header>
      <main>
        {section === "ota" ? <Ota /> : section === "node" ? <NodeDetail gw={Number(a)} node={Number(b)} /> : <Fleet />}
      </main>
    </>
  );
}
