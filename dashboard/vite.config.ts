import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

// `npm run dev` proxies the API so the SPA and Go service can run on different ports.
export default defineConfig({
  plugins: [react()],
  server: { proxy: { "/api": { target: process.env.FLEETBUS_API ?? "http://localhost:8080", changeOrigin: true } } },
});
