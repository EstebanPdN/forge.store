import { API } from './ghapi.js';
// Live refresh from the GitHub REST API: the newest stable / pre-release tags
// with their real asset names and sizes. Results are
// cached for six hours to stay well inside the unauthenticated rate limit.
// The 3DS version does the same with libcurl + mbedTLS (api.github.com needs TLS 1.2).

// Unauthenticated GitHub calls are limited to 60 per hour per IP, shared with the
// built-in GitHub viewer, so the catalog refresh uses one call per repo every 6 hours.
const TTL = 6 * 3600 * 1000;

export async function refreshFromGitHub(catalog, state) {
  const cache = state.liveCache;
  let repos = cache && Date.now() - cache.at < TTL ? cache.repos : null;
  if (!repos) {
    repos = {};
    // Only the curated catalog; each repo succeeds or fails on its own, so one
    // rate-limited request doesn't throw the rest away.
    const apps = catalog.data.apps.filter((a) => a.repo && a.releases);
    const results = await Promise.allSettled(
      apps.map(async (app) => {
        const releases = await fetchJSON(`${API}/repos/${app.repo}/releases?per_page=10`);
        repos[app.repo] = {
          releases: releases
            .filter((r) => !r.draft)
            .map((r) => ({
              tag: r.tag_name,
              pre: r.prerelease,
              date: r.published_at.slice(0, 10),
              assets: r.assets.map((a) => ({ name: a.name, size: a.size })),
            })),
        };
      }),
    );
    const failed = results.filter((r) => r.status === 'rejected').length;
    if (failed) console.warn(`[github] ${failed} of ${apps.length} repos could not be refreshed (offline or rate-limited)`);
    if (!Object.keys(repos).length) return { ok: false };
    state.liveCache = { at: Date.now(), repos };
    state.save();
  }
  for (const app of catalog.apps) {
    const live = repos[app.repo];
    if (!live) continue;
    if (live.stars != null) app.stars = live.stars;
    if (app.releasePinned) continue;
    const releases = live.releases.filter((r) => !r.tag.startsWith('store-media-'));
    merge(app, 'stable', releases.find((r) => !r.pre));
    const stable = app.releases.stable;
    const pre = releases.find((r) => r.pre && (!stable || r.date > stable.date));
    if (pre) merge(app, 'experimental', pre);
  }
  catalog.rev++;
  return { ok: true };
}

async function fetchJSON(url) {
  const res = await fetch(url, { headers: { Accept: 'application/vnd.github+json' } });
  if (!res.ok) throw new Error(`${res.status} ${url}`);
  return res.json();
}

/** Maps the catalog's asset keys onto the live release by swapping the tag in each name. */
function merge(app, channel, live) {
  if (!live) return;
  const known = app.releases[channel] || app.releases.stable || app.releases.experimental;
  if (!known || known.tag === live.tag) return;
  const assets = {};
  for (const [key, asset] of Object.entries(known.assets)) {
    const guess = asset.name.replace(known.tag, live.tag).replace(known.tag.replace(/^v/, ''), live.tag.replace(/^v/, ''));
    const match = live.assets.find((a) => a.name === guess) || live.assets.find((a) => a.name === asset.name);
    if (match) assets[key] = { ...asset, name: match.name, size: match.size };
  }
  if (!Object.keys(assets).length) return;
  app.releases[channel] = {
    ...known,
    tag: live.tag,
    date: live.date,
    assets,
    notes: { es: ['Versión publicada en GitHub (notas en el repositorio).'], en: ['Published on GitHub (notes in the repository).'] },
  };
}
