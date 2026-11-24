const { chromium } = require('playwright');
(async () => {
  const b = await chromium.launch();
  const errors = [];
  for (const [file, out, steps] of [[process.argv[2], 'g', 5], [process.argv[3], 't', 1]]) {
    const p = await b.newPage({ viewport: { width: 1300, height: 1150 } });
    p.on('pageerror', e => errors.push(file + ' ' + String(e)));
    await p.goto('file://' + file);
    for (let i = 0; i < steps; i++) await p.keyboard.press('ArrowRight');
    await p.screenshot({ path: process.argv[4] + '-' + out + '-en.png', fullPage: true });
    await p.click('#lang button[data-lang="fr"]');
    if (out === 'g') await p.click('#moves .var a');
    await p.screenshot({ path: process.argv[4] + '-' + out + '-fr.png', fullPage: true });
    console.log(out, await p.evaluate(() => document.getElementById('lead').textContent + ' | ' + document.getElementById('info').textContent + ' | ' + document.getElementById('pos').textContent + ' | ' + document.getElementById('stamp').textContent));
  }
  console.log('erreurs :', JSON.stringify(errors));
  await b.close();
})();
