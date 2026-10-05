import { StrictMode } from "react";
import { createRoot } from "react-dom/client";
import App from "./App";

export function mount(host: HTMLElement): () => void {
  const root = createRoot(host);
  root.render(
    <StrictMode>
      <App />
    </StrictMode>,
  );
  return () => root.unmount();
}
export const unmount = mount(document.getElementById("root")!);
if (import.meta.hot) import.meta.hot.dispose(unmount);
