import { defineConfig } from "vite";
import vue from "@vitejs/plugin-vue";
const headers = {
  "Cross-Origin-Opener-Policy": "same-origin",
  "Cross-Origin-Embedder-Policy": "require-corp",
};
export default defineConfig({
  base: process.env.VIEWER_BASE ?? "/",
  plugins: [vue()],
  server: { host: "127.0.0.1", headers },
  preview: { headers },
});
