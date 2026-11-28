// Render the manual (dendrite-manual.html, A4 pages) to dendrite-manual.pdf with headless Chromium.
//
//     node docs/manual/render_manual.js [previewdir]
//
// With a directory, each page is also written there as page-NN.png. Fails if anything runs off its page.
// Needs Playwright (npm i playwright) and its Chromium.
const fs = require("fs");
const path = require("path");
const { pathToFileURL } = require("url");
const { chromium } = require("playwright");

(async () => {
  const here = __dirname;
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 794, height: 1123 }, deviceScaleFactor: 2 });
  await page.goto(pathToFileURL(path.join(here, "dendrite-manual.html")).href);
  await page.evaluate(() => document.fonts.ready);
  await page.waitForLoadState("networkidle");
  const over = await page.evaluate(() => {
    const bad = [];
    document.querySelectorAll(".page").forEach((p, i) => {
      const body = p.querySelector(".body");
      const folio = p.querySelector(".folio");
      if (!body) return;
      const limit = folio ? folio.getBoundingClientRect().top - 2 : p.getBoundingClientRect().bottom;
      let bottom = 0;
      body.querySelectorAll("*").forEach((e) => { bottom = Math.max(bottom, e.getBoundingClientRect().bottom); });
      if (bottom > limit) bad.push(`page ${i + 1}: ${Math.round(bottom - limit)} px too long`);
    });
    return bad;
  });
  if (over.length) {
    console.error(over.join("\n"));
    process.exitCode = 1;
  }
  if (process.argv[2]) {
    const dir = process.argv[2];
    fs.mkdirSync(dir, { recursive: true });
    const pages = await page.$$(".page");
    for (let i = 0; i < pages.length; ++i)
      await pages[i].screenshot({ path: path.join(dir, `page-${String(i + 1).padStart(2, "0")}.png`) });
  }
  if (!over.length)
    await page.pdf({ path: path.join(here, "dendrite-manual.pdf"), format: "A4", printBackground: true,
                     margin: { top: "0", right: "0", bottom: "0", left: "0" } });
  await browser.close();
})();
