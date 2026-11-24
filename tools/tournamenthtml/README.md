# Tools — HTML page of a tournament

To check a page produced by **Tournament → Export as an HTML page…** (`src/ui/tournamenthtml.*`).

- `node check.js page.html`: rereads the page's data and replays each game with the page's script
  up to the expected final position.
- `node shot.js game.html tournament.html prefix` (absolute paths): screenshots of both pages in
  English then in French, after a few moves and a click on a variation. Requires Playwright:
  `npm install playwright && npx playwright install chromium` in a working folder.
