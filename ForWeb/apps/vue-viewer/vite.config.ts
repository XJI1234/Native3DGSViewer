import { defineConfig } from "vite";
import vue from "@vitejs/plugin-vue";
export default defineConfig({
  base: process.env.VIEWER_BASE ?? "/",
  plugins: [vue()],
  server: { host: "127.0.0.1" },
});
