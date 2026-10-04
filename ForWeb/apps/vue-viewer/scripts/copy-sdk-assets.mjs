import { cp, mkdir, readFile, rm } from "node:fs/promises";
import { createHash } from "node:crypto";
import { dirname, resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";
const assets = resolve(
  dirname(fileURLToPath(import.meta.resolve("@native3dgs/web"))),
  "assets",
);
const manifest = JSON.parse(
  await readFile(resolve(assets, "manifest.json"), "utf8"),
);
for (const file of manifest.files) {
  const bytes = await readFile(resolve(assets, file.name));
  if (createHash("sha256").update(bytes).digest("hex") !== file.sha256)
    throw Error("SDK asset mismatch: " + file.name);
}
const projectRoot = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const destination = resolve(projectRoot, "public/gs-assets");
if (!destination.startsWith(projectRoot + sep))
  throw Error("Generated asset path escaped project");
await mkdir(resolve(projectRoot, "public"), { recursive: true });
await rm(destination, { recursive: true, force: true });
await cp(assets, destination, { recursive: true });
console.log("Copied verified installed SDK WASM, Worker and licenses.");
