import { defineConfig } from "vite";
const headers = {
  "Cross-Origin-Opener-Policy": "same-origin",
  "Cross-Origin-Embedder-Policy": "require-corp",
};
export default defineConfig({
  base: process.env.VIEWER_BASE ?? "/",
  server: { host: "127.0.0.1", headers },
  preview: { headers },
});
