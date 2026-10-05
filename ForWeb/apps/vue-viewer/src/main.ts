import { createApp } from "vue";
import App from "./App.vue";

export function mount(host: HTMLElement): () => void {
  const app = createApp(App);
  app.mount(host);
  return () => app.unmount();
}
export const unmount = mount(document.getElementById("root")!);
if (import.meta.hot) import.meta.hot.dispose(unmount);
