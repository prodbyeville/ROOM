// Prints the ROOM guide (docs/guide/guide.html) to dist/ROOM-Guide.pdf with Playwright's Chromium.
//   node plugin/docs/guide/build-guide.mjs [out.pdf]
import { createRequire } from "node:module";
const { chromium } = createRequire(import.meta.url)("playwright");
import { fileURLToPath, pathToFileURL } from "node:url";
import path from "node:path";
import fs from "node:fs";
const here = path.dirname(fileURLToPath(import.meta.url));
const out = process.argv[2] ?? path.join(here, "ROOM-Guide.pdf");   // committed: the release zips bundle it
fs.mkdirSync(path.dirname(out), { recursive: true });
const browser = await chromium.launch({ args: ["--allow-file-access-from-files"] });
const page = await browser.newPage();
await page.goto(pathToFileURL(path.join(here, "guide.html")).href, { waitUntil: "networkidle" });
await page.evaluate(() => document.fonts.ready);
await page.pdf({ path: out, width: "8.5in", height: "11in", printBackground: true, preferCSSPageSize: true });
await browser.close();
console.log("Guide:", out);
