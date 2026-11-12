// Read-only client for GitHub's public REST API, used by the built-in GitHub viewer.
// Rendered HTML (README, release notes) comes straight from GitHub; responses are
// served from the reviewed bundle or localStorage immediately; stale data refreshes
// in the background. On 3DS: libcurl + mbedTLS and the persistent SD cache.

// Through the local server's caching proxy when there is one (tools/serve.py).
export const API = ['localhost', '127.0.0.1'].includes(location.hostname) ? '/gh-api' : 'https://api.github.com';
const HOUR = 3600 * 1000;
const CACHE = 'forge-store/gh/';

export class RateLimitError extends Error {
  constructor(reset) {
    super('rate-limited');
    this.reset = reset;
  }
}

// Ship a reviewed local baseline. Revalidation never delays opening a page.
const bundled = fetch('assets/reader-data/pages.json').then((r) => r.ok ? r.json() : null).catch(() => null);
let bundledImages = {};
const imageMapReady = fetch('assets/reader-art/urls.json').then((r) => r.json()).then((v) => { bundledImages = v; }).catch(() => {});
export const githubImage = (url) => bundledImages[url] || bundledImages[url.replace(/&s=\d+$/, '')] || url;
const refreshing = new Map();
async function refresh(path, accept, key) {
  if (refreshing.has(key)) return refreshing.get(key);
  const request = (async () => {
    const res = await fetch(API + path, { headers: { Accept: accept }, signal:AbortSignal.timeout(6000) });
    if (res.status === 403 || res.status === 429) throw new RateLimitError(Number(res.headers.get('x-ratelimit-reset')) * 1000 || Date.now()+HOUR);
    if (res.status === 404) return null;
    if (!res.ok) throw new Error(`${res.status} ${path}`);
    const data = accept.includes('html') && !accept.includes('json') ? await res.text() : await res.json();
    try { localStorage.setItem(key,JSON.stringify({at:Date.now(),data})); } catch {}
    return data;
  })();
  refreshing.set(key,request);
  try { return await request; } finally { refreshing.delete(key); }
}
async function get(path, accept = 'application/vnd.github+json') {
  await imageMapReady;
  const key = CACHE + accept.split('.').pop() + path;
  const ttl = path.endsWith('/readme') ? 7*24*HOUR : 6*HOUR;
  let hit;
  try { hit=JSON.parse(localStorage.getItem(key)); } catch {}
  if (hit) { if(Date.now()-hit.at>=ttl)refresh(path,accept,key).catch(()=>{}); return hit.data; }
  const [local] = await Promise.all([bundled,imageMapReady]);
  const id = (accept.includes('html')&&!accept.includes('json')?'html:':'json:')+path;
  if (local && Object.hasOwn(local.pages,id)) {
    if (Date.now()-Date.parse(local.captured_at)>=ttl)refresh(path,accept,key).catch(()=>{});
    return local.pages[id];
  }
  return refresh(path,accept,key);
}

export const github = {
  repo: (repo) => get(`/repos/${repo}`),
  readme: (repo) => get(`/repos/${repo}/readme`, 'application/vnd.github.html'),
  releases: (repo) => get(`/repos/${repo}/releases?per_page=12`, 'application/vnd.github.html+json'),
  commits: (repo) => get(`/repos/${repo}/commits?per_page=10`),
  user: (login) => get(`/users/${login}`),
  // The profile page is the README of the special <login>/<login> repository.
  profileReadme: (login) => get(`/repos/${login}/${login}/readme`, 'application/vnd.github.html'),
  userRepos: (login) => get(`/users/${login}/repos?sort=pushed&per_page=40`),
};

/**
 * GitHub already sanitises rendered markdown, but the viewer is strict anyway:
 * no scripts, frames, forms or inline handlers, and relative links and images are
 * resolved against the repository.
 */
export function cleanHTML(html, repo, branch = 'HEAD') {
  const doc = new DOMParser().parseFromString(`<div>${html || ''}</div>`, 'text/html');
  const root = doc.body.firstElementChild;
  root.querySelectorAll('script, style, iframe, object, embed, form, input, button, link, meta').forEach((el) => el.remove());
  for (const el of root.querySelectorAll('*')) {
    for (const attr of [...el.attributes]) {
      const name = attr.name.toLowerCase();
      if (name.startsWith('on') || (['href', 'src'].includes(name) && /^\s*javascript:/i.test(attr.value))) el.removeAttribute(attr.name);
    }
  }
  const raw = (src) => {
    if(src.startsWith("http://"))src="https://"+src.slice(7);
    const blob = src.match(/^https:\/\/github\.com\/([^/]+\/[^/]+)\/blob\/(.+)$/);
    if (blob) return `https://raw.githubusercontent.com/${blob[1]}/${blob[2]}`;
    return /^(https?:|data:)/.test(src) ? src : `https://raw.githubusercontent.com/${repo}/${branch}/${src.replace(/^\.?\//, '')}`;
  };
  // <picture> picks its <source> with the OS colour scheme and relative paths would
  // resolve against the store: choose the variant for the store's theme instead.
  const dark = document.documentElement.dataset.theme === 'dark';
  for (const picture of root.querySelectorAll('picture')) {
    const img = picture.querySelector('img');
    const source = [...picture.querySelectorAll('source')].find((el) => (el.getAttribute('media') || '').includes(dark ? 'dark' : 'light'));
    if (img && source) img.setAttribute('src', source.getAttribute('srcset').split(/\s+/)[0]);
    picture.querySelectorAll('source').forEach((el) => el.remove());
  }
  // QR codes are for phones; on the console they only take space.
  for (const img of root.querySelectorAll('img')) {
    if (/\bqr\b|qr[-_ ]?code|-qr\./i.test(`${img.getAttribute('alt') || ''} ${img.getAttribute('src') || ''}`)) {
      const link = img.closest('a');
      (link && link.textContent.trim() === '' ? link : img).remove();
    }
  }
  for (const img of root.querySelectorAll('img')) {
    const src = img.getAttribute('src');
    if (src) img.setAttribute('src', raw(src));
    img.setAttribute('loading', 'lazy');
    img.removeAttribute('width');
    img.removeAttribute('height');
  }
  for (const a of root.querySelectorAll('a[href]')) {
    const href = a.getAttribute('href');
    if (href.startsWith('#')) continue;
    if (!/^https?:/.test(href)) a.setAttribute('href', `https://github.com/${repo}/blob/${branch}/${href.replace(/^\.?\//, '')}`);
    a.setAttribute('target', '_blank');
    a.setAttribute('rel', 'noopener');
  }
  // Use native-size text instead of shrinking large external support SVGs.
  for (const a of root.querySelectorAll('a')) {
    const img = a.querySelector('img');
    if (!img) continue;
    const src = img.getAttribute('src') || '';
    const label = /website/i.test(src) ? 'Website' : /discord/i.test(src) ? 'Discord' : /patreon/i.test(src) ? 'Patreon' : null;
    if (!label) continue;
    a.replaceChildren(doc.createTextNode(label));
    a.classList.add('gh-support-link');
  }
  for (const block of root.querySelectorAll('p, div')) {
    const children = [...block.children].filter((el) => el.tagName !== 'BR');
    if (children.length && children.every((el) => el.classList.contains('gh-support-link'))) {
      block.classList.add('gh-support-row');
      block.querySelectorAll('br').forEach((el) => el.remove());
    }
  }
  // Rows made only of linked images (website / Discord / Patreon buttons, badges)
  // become one compact line instead of full-size buttons.
  for (const block of root.querySelectorAll('p, div')) {
    const kids = [...block.children];
    const onlyLinkedImages =
      kids.length > 1 &&
      kids.every((el) => (el.tagName === 'A' && el.querySelector('img') && el.textContent.trim() === '') || el.tagName === 'IMG' || el.tagName === 'BR') &&
      [...block.childNodes].every((n) => n.nodeType !== 3 || !n.textContent.replace(/[\s·|]/g, ''));
    if (onlyLinkedImages) {
      block.classList.add('gh-imgrow');
      block.querySelectorAll('br').forEach((br) => br.remove());
    }
  }
  for (const img of root.querySelectorAll('img[src]')) img.src=githubImage(img.getAttribute('src'));
  return root;
}
