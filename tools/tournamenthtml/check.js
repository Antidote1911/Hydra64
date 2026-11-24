const fs = require('fs');
const h = fs.readFileSync(process.argv[2], 'utf8');
const d0 = h.indexOf('id="data">') + 10, d1 = h.indexOf('</script>', d0);
const D = JSON.parse(h.slice(d0, d1));
const s0 = h.indexOf('<script>', d1) + 8, s1 = h.indexOf('</script>', s0);
eval(h.slice(s0, s1) + '\n;globalThis.F={gameTree,finalPlacement};');
let bad = 0, moves = 0, evals = 0;
for (const g of D.games) { if (F.finalPlacement(g) !== g.e) bad++; for (const m of g.m || []) { moves++; if (m[5] && m[5].e !== undefined) evals++; } }
console.log(D.mode + ': ' + D.games.length + ' game(s), ' + moves + ' moves, ' + evals + ' with evaluation, ' + bad + ' wrong replay(s)');
