// SPDX-License-Identifier: GPL-3.0-only
// Serve the browser FM-1 on http://localhost:8137: the page from web/, the app from build/web/juicy.wasm.
// The two headers make the page "cross-origin isolated", which the shared memory between the page, the worker
// and the audio needs.
//   node web/serve.js
const http = require("http");
const fs = require("fs");
const path = require("path");

const PORT = 8137;
const ROOTS = [__dirname, path.join(__dirname, "..", "build", "web")];
const TYPES = { ".html": "text/html", ".js": "text/javascript", ".wasm": "application/wasm" };

http.createServer((request, response) => {
    const name = path.basename(request.url.split("?")[0]) || "index.html";
    const file = ROOTS.map((root) => path.join(root, name)).find((candidate) => fs.existsSync(candidate));
    if (!file || !TYPES[path.extname(file)]) {
        response.writeHead(404);
        response.end("not found");
        return;
    }
    response.writeHead(200, {
        "Content-Type": TYPES[path.extname(file)],
        "Cross-Origin-Opener-Policy": "same-origin",
        "Cross-Origin-Embedder-Policy": "require-corp",
        "Cache-Control": "no-store",
    });
    response.end(fs.readFileSync(file));
}).listen(PORT, "127.0.0.1", () => console.log(`Juicy on http://localhost:${PORT}`));
