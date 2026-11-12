// Read-only client for GitHub's public REST API, used by the built-in GitHub viewer.
// Rendered HTML (README, release notes) comes straight from GitHub; responses are
// cached for an hour in localStorage because unauthenticated calls are limited to
// 60 per hour. On 3DS: libcurl + mbedTLS, and the cache lives on the SD card.

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

async function get(path, accept = 'application/vnd.github+json') {
  const key = CACHE + accept.split('.').pop() + path;
  try {
    const hit = JSON.parse(localStorage.getItem(key));
    if (hit && Date.now() - hit.at < HOUR) return hit.data;
  } catch {}
  const res = await fetch(API + path, { headers: { Accept: accept } });
  if (res.status === 403 || res.status === 429) {
    throw new RateLimitError(Number(res.headers.get('x-ratelimit-reset')) * 1000 || Date.now() + HOUR);
  }
  if (res.status === 404) return null;
  if (!res.ok) throw new Error(`${res.status} ${path}`);
  const data = accept.includes('html') && !accept.includes('json') ? await res.text() : await res.json();
  try {
    localStorage.setItem(key, JSON.stringify({ at: Date.now(), data }));
  } catch {
    // Storage full: drop old GitHub entries and carry on without caching.
    Object.keys(localStorage).filter((k) => k.startsWith(CACHE)).forEach((k) => localStorage.removeItem(k));
  }
  return data;
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
  const raw = (src) => (/^(https?:|data:)/.test(src) ? src : `https://raw.githubusercontent.com/${repo}/${branch}/${src.replace(/^\.?\//, '')}`);
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
  return root;
}
