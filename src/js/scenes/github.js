// Built-in GitHub viewer: a 3DS-sized reader for a repository's public data
// (README, releases, details and recent commits) from GitHub's REST API.
// Top screen: the page. Bottom screen: tabs and a touchpad that scrolls it.
import { Scene } from './scene.js';
import { h, animate, clamp } from '../core/dom.js';
import { t, fmtDate, fmtSize, fmtStars } from '../core/i18n.js';
import { icon } from '../core/icons.js';
import { Scroller } from '../ui/components.js';
import { TabBar, revealTabContent } from '../ui/navigation.js';
import { github, githubImage, cleanHTML, RateLimitError } from '../core/ghapi.js';

const nativeProfile = fetch('assets/reader-data/profile.json').then((r) => r.json()).catch(() => null);
const REPO_TABS = ['readme', 'releases', 'info'];
const USER_TABS = ['profile', 'repos', 'about'];

export class GitHubScene extends Scene {
  static section = 'github';

  build() {
    // Two modes: a repository, or a user's profile page (README, repositories, details).
    this.user = this.params.user || null;
    this.repoName = this.params.repo || this.user;
    this.tabs = this.user ? USER_TABS : REPO_TABS;
    this.tab = this.tabs[0];
    this.data = {};
    this.focus = 0;

    // Top screen: one scrolling page.
    this.page = h('div.scroll-content.gh-page');
    this.view = h('div.gh-view.scroller', this.page);
    this.scroller = new Scroller(this.view, this.page);
    const sync = this.scroller.syncBar.bind(this.scroller);
    this.scroller.syncBar = () => {
      sync();
      this.syncPad();
    };
    this.top.append(h('div.bg-forge'), this.view);

    // Bottom screen: bar, tabs, touchpad.
    this.tabBar = new TabBar(this.tabs.map((id) => ({ value: id, label: t('gh.' + id) })), this.tab, (v) => this.setTab(v));
    this.tabsSeg = this.tabBar.el;
    this.disposers.push(() => this.tabBar.destroy());
    this.thumb = h('i.gh-thumb');
    this.percent = h('span.gh-percent', '0 %');
    this.pad = h(
      'div.gh-pad',
      h('div.gh-track', this.thumb),
      h('div.gh-pad-hint', icon('layers'), h('b', t('gh.pad')), h('small', t('gh.padHint'))),
      this.percent,
    );
    this.pad.addEventListener('pointerdown', (e) => this.scroller.drag(e));
    this.pad.addEventListener('wheel', (e) => {
      e.preventDefault();
      this.scroller.by(e.deltaY * 0.6);
    }, { passive: false });
    this.bottom.append(
      h('div.bg-forge'),
      h(
        'div.pn-bar',
        h('button.dt-back', { type: 'button', 'aria-label': t('action.back'), on: { click: () => this.leaveScene() } }, icon('back')),
        h('div.pn-bar-title.gh-bar-title', icon('github'), h('span.ellipsis', this.repoName)),
      ),
      h('div.gh-tabs', this.tabsSeg),
      this.pad,
      h(
        'div.gh-buttons',
        h('button.ui-btn.small', { type: 'button', on: { click: () => this.scroller.by(-150) } }, icon('update'), t('gh.pageUp')),
        h('button.ui-btn.small', { type: 'button', on: { click: () => this.scroller.by(150) } }, icon('download'), t('gh.pageDown')),
        h('a.ui-btn.small.ghost', { href: `https://github.com/${this.repoName}`, target: '_blank', rel: 'noopener' }, icon('link'), 'github.com'),
      ),
    );
    this.load();
  }

  async load() {
    this.renderLoading();
    if (this.user) return this.loadUser();
    try {
      const repo = await github.repo(this.repoName);
      if (!repo) return this.renderError(t('gh.notFound'));
      this.data.repo = repo;
      for (const app of this.catalog.apps) if (app.repo === this.repoName) app.stars = repo.stargazers_count;
      this.renderTab();
      const [readme, releases, commits] = await Promise.allSettled([
        github.readme(this.repoName),
        github.releases(this.repoName),
        github.commits(this.repoName),
      ]);
      const limited = [readme, releases, commits].find((r) => r.status === 'rejected' && r.reason instanceof RateLimitError);
      this.data.readme = readme.status === 'fulfilled' ? readme.value : null;
      this.data.releases = releases.status === 'fulfilled' ? releases.value || [] : null;
      this.data.commits = commits.status === 'fulfilled' ? commits.value || [] : null;
      if (limited) this.data.limit = limited.reason.reset;
      this.renderTab(true);
    } catch (error) {
      console.warn('[github viewer]', error);
      this.renderError(error instanceof RateLimitError ? t('gh.rateLimit', { time: new Date(error.reset).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' }) }) : t('gh.error'));
    }
  }

  async loadUser() {
    if (this.user === 'EstebanPdN') this.data.nativeProfile = await nativeProfile;
    try {
      const user = await github.user(this.user);
      if (!user) return this.renderError(t('gh.notFound'));
      this.data.user = user;
      this.renderTab();
      const [readme, repos] = await Promise.allSettled([github.profileReadme(this.user), github.userRepos(this.user)]);
      this.data.readme = readme.status === 'fulfilled' ? readme.value : null;
      this.data.repos = repos.status === 'fulfilled' ? (repos.value || []).filter((r) => !r.fork) : null;
      const limited = [readme, repos].find((r) => r.status === 'rejected' && r.reason instanceof RateLimitError);
      if (limited) this.data.limit = limited.reason.reset;
      this.renderTab(true);
    } catch (error) {
      console.warn('[github viewer]', error);
      this.renderError(error instanceof RateLimitError ? t('gh.rateLimit', { time: new Date(error.reset).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' }) }) : t('gh.error'));
    }
  }

  // --- rendering ----------------------------------------------------------------
  nativeProfileView() {
    const p = this.data.nativeProfile, u = this.data.user;
    const value = (name) => p.rows.find((r) => r.label === name)?.value || '';
    const row = (label, name) => h('div.profile-row', h('b', label), h('span', value(name)));
    const stars = (this.data.repos || []).reduce((sum, r) => sum + r.stargazers_count, 0);
    return h('div.native-profile',
      h('div.profile-terminal.glass',
        h('img.profile-portrait', {src:'assets/reader-data/portrait.png',alt:''}),
        h('div.profile-system', h('b.profile-prompt',p.login),...['OS','Hardware','IDE'].map((k) => row(k,k)))),
      h('div.gh-support-row',p.links.map((l) => h('a.gh-support-link',{href:l.url,target:'_blank',rel:'noopener'},l.label))),
      h('div.profile-details.glass',row('Code','Languages.Programming'),row('Web','Languages.Computer'),row('Languages','Languages.Real'),row('Software','Hobbies.Software'),row('Hardware','Hobbies.Hardware')),
      h('div.profile-stats',[[u.public_repos,t('gh.repos')],[stars,t('gh.totalStars')],[u.followers,t('gh.followers')]].map(([n,label]) => h('div.glass',h('b',String(n)),h('span',label)))),
      h('div.profile-contact',h('b.profile-prompt','Contact'),h('p',value('Email')),h('p',value('Discord'))),
      h('div.profile-development',h('b.profile-prompt','Development'),h('p',p.development),h('a',{href:p.developmentUrl,target:'_blank',rel:'noopener'},'Learn about the development process')));
  }

  userHeader() {
    const u = this.data.user;
    const stats = [
      [icon('people'), `${fmtStars(u.followers)} ${t('gh.followers')}`],
      [icon('box'), `${u.public_repos} ${t('gh.repos')}`],
      u.location ? [icon('globe'), u.location] : null,
    ].filter(Boolean);
    return h(
      'div.gh-head.user',
      h('img.gh-avatar.round', { src: githubImage(u.avatar_url), alt: '' }),
      h(
        'div.gh-head-text',
        h('div.gh-name', h('b', u.name || u.login), u.name ? h('span.faint', ' @' + u.login) : null),
        u.bio ? h('p.gh-desc', u.bio) : null,
        h('div.gh-stats', stats.map(([ic, v]) => h('span', ic, v))),
      ),
    );
  }

  reposView() {
    const list = this.data.repos;
    if (list === undefined) return h('div.gh-state', h('span.spinner'), t('gh.loading'));
    if (!list?.length) return h('div.gh-state', icon('box'), '—');
    this.focus = clamp(this.focus, 0, list.length - 1);
    this.repoRows = list.map((r, i) =>
      h(
        'div.gh-repo' + (i === this.focus ? '.focus' : ''),
        h('div.gh-repo-head', icon('box'), h('b.ellipsis', r.name)),
        r.description ? h('p', r.description) : null,
        h(
          'div.gh-stats',
          r.language ? h('span.lang', h('i'), r.language) : null,
          h('span', icon('star'), fmtStars(r.stargazers_count)),
          h('span', icon('people'), fmtStars(r.forks_count)),
          h('span', icon('clock'), fmtDate(r.pushed_at.slice(0, 10))),
        ),
      ),
    );
    return h('div.gh-repos', h('div.eyebrow.dt-eyebrow', t('gh.reposTitle', { n: list.length })), this.repoRows);
  }

  aboutView() {
    const u = this.data.user;
    const facts = [
      [t('gh.followers'), fmtStars(u.followers)],
      [t('gh.following'), fmtStars(u.following)],
      [t('gh.repos'), String(u.public_repos)],
      [t('gh.joined'), fmtDate(u.created_at.slice(0, 10))],
      [t('gh.updated'), fmtDate(u.updated_at.slice(0, 10))],
      [t('gh.location'), u.location || '—'],
    ];
    const stars = (this.data.repos || []).reduce((sum, r) => sum + r.stargazers_count, 0);
    if (stars) facts.push([t('gh.totalStars'), fmtStars(stars)]);
    return h(
      'div.gh-info',
      h('div.dt-facts', facts.map(([k, v]) => h('div.fact', h('span.k', k), h('span.v', v)))),
      u.blog ? h('p.gh-home', icon('globe'), u.blog) : null,
      u.company ? h('p.gh-home', icon('people'), u.company) : null,
    );
  }

  moveRepo(dir) {
    if (!this.repoRows?.length) return;
    const next = clamp(this.focus + dir, 0, this.repoRows.length - 1);
    if (next === this.focus) return this.audio.sfx('move', { pitch: -5 });
    this.focus = next;
    this.audio.sfx('move');
    const y = this.scroller.y;
    this.renderTab(true);
    this.scroller.scrollTo(y, false);
    this.scroller.reveal(this.repoRows[next], 10);
  }

  openRepo() {
    const repo = this.data.repos?.[this.focus];
    if (!repo) return;
    this.audio.sfx('select');
    this.app.open('github', { repo: repo.full_name });
  }

  header() {
    const r = this.data.repo;
    const stats = [
      [icon('star'), fmtStars(r.stargazers_count)],
      [icon('people'), fmtStars(r.forks_count)],
      [icon('eye'), fmtStars(r.subscribers_count ?? r.watchers_count)],
      [icon('warn'), String(r.open_issues_count)],
    ];
    return h(
      'div.gh-head',
      h('img.gh-avatar', { src: githubImage(r.owner.avatar_url), alt: '' }),
      h(
        'div.gh-head-text',
        h('div.gh-name', h('span.faint', r.owner.login + ' / '), h('b', r.name)),
        r.description ? h('p.gh-desc', r.description) : null,
        h('div.gh-stats', stats.map(([ic, v]) => h('span', ic, v)), r.language ? h('span.lang', h('i'), r.language) : null),
      ),
    );
  }

  renderLoading() {
    this.page.replaceChildren(h('div.gh-state', h('span.spinner'), t('gh.loading')));
  }

  renderError(text) {
    this.page.replaceChildren(h('div.gh-state', icon('wifi'), h('b', text), h('small', 'github.com/' + this.repoName)));
    this.scroller.reset();
  }

  setTab(id) {
    if (id === this.tab) return;
    const direction = Math.sign(this.tabs.indexOf(id) - this.tabs.indexOf(this.tab));
    this.tab = id;
    this.tabsSeg.seg.set(id);
    this.audio.sfx('tab');
    this.renderTab();
    revealTabContent(this.page.firstElementChild?.nextElementSibling, direction);
  }

  renderTab(keep = false) {
    if (!this.data.repo && !this.data.user) return;
    const y = this.scroller.y;
    const views = {
      readme: () => this.readmeView(),
      releases: () => this.releasesView(),
      info: () => this.infoView(),
      profile: () => this.data.nativeProfile ? this.nativeProfileView() : this.readmeView(),
      repos: () => this.reposView(),
      about: () => this.aboutView(),
    };
    const body = views[this.tab]();
    const custom = this.tab === 'profile' && this.data.nativeProfile;
    this.page.replaceChildren(...(custom ? [body] : [this.user ? this.userHeader() : this.header(), body]));
    if (this.data.limit) this.page.append(h('p.gh-note', icon('clock'), t('gh.partial')));
    if (keep) this.scroller.scrollTo(y, false);
    else {
      this.scroller.reset();
      revealTabContent(body);
    }
    // Images load late and change the page height.
    this.page.querySelectorAll('img').forEach((img) => img.addEventListener('load', () => this.scroller.syncBar(), { once: true }));
    requestAnimationFrame(() => this.scroller.syncBar());
  }

  readmeView() {
    if (this.data.readme === undefined) return h('div.gh-state', h('span.spinner'), t('gh.loading'));
    if (!this.data.readme) return h('div.gh-state', icon('file'), t('gh.noReadme'));
    const md = cleanHTML(this.data.readme, this.user ? `${this.user}/${this.user}` : this.repoName, this.data.repo?.default_branch || 'HEAD');
    md.className = 'gh-md';
    return md;
  }

  releasesView() {
    const list = this.data.releases;
    if (list === undefined) return h('div.gh-state', h('span.spinner'), t('gh.loading'));
    if (!list?.length) return h('div.gh-state', icon('box'), t('gh.noReleases'));
    return h(
      'div.gh-releases',
      list.map((rel, i) => {
        const notes = cleanHTML(rel.body_html || '', this.repoName, this.data.repo.default_branch);
        notes.className = 'gh-md small';
        return h(
          'div.gh-release',
          h(
            'div.gh-release-head',
            h('b', rel.name || rel.tag_name),
            h('span.chip' + (rel.prerelease ? '.lav' : '.mint'), rel.prerelease ? t('channel.experimental') : t('channel.stable')),
            i === 0 ? h('span.chip.forge', t('detail.latest')) : null,
            h('span.faint', fmtDate(rel.published_at.slice(0, 10))),
          ),
          rel.assets.length
            ? h('div.gh-assets', rel.assets.slice(0, 6).map((a) => h('span.gh-asset', icon(/\.cia$/i.test(a.name) ? 'cia' : 'file'), h('span.ellipsis', a.name), h('small', fmtSize(a.size)))))
            : null,
          notes,
        );
      }),
    );
  }

  infoView() {
    const r = this.data.repo;
    const facts = [
      [t('gh.language'), r.language || '—'],
      [t('detail.license'), r.license?.spdx_id && r.license.spdx_id !== 'NOASSERTION' ? r.license.spdx_id : r.license?.name || '—'],
      [t('gh.branch'), r.default_branch],
      [t('gh.updated'), fmtDate(r.pushed_at.slice(0, 10))],
      [t('gh.created'), fmtDate(r.created_at.slice(0, 10))],
      [t('gh.size'), fmtSize(r.size * 1024)],
    ];
    const commits = this.data.commits;
    return h(
      'div.gh-info',
      r.topics?.length ? h('div.gh-topics', r.topics.slice(0, 10).map((topic) => h('span.chip.lav', topic))) : null,
      h('div.dt-facts', facts.map(([k, v]) => h('div.fact', h('span.k', k), h('span.v', v)))),
      r.homepage ? h('p.gh-home', icon('globe'), r.homepage) : null,
      h('div.eyebrow.dt-eyebrow', t('gh.commits')),
      commits === undefined
        ? h('div.gh-state', h('span.spinner'))
        : commits?.length
          ? h(
              'div.gh-commits',
              commits.map((c) =>
                h(
                  'div.gh-commit',
                  c.author?.avatar_url ? h('img', { src: c.author.avatar_url + '&s=32', alt: '' }) : h('i.gh-dot'),
                  h('div', h('b.ellipsis', c.commit.message.split('\n')[0]), h('small', `${c.commit.author.name} · ${fmtDate(c.commit.author.date.slice(0, 10))} · ${c.sha.slice(0, 7)}`)),
                ),
              ),
            )
          : h('p.faint', '—'),
    );
  }

  /** Mirrors the page position on the touchpad's track. */
  syncPad() {
    const view = this.view.clientHeight;
    const total = Math.max(view, this.page.scrollHeight);
    const trackH = 104;
    const thumbH = clamp((view / total) * trackH, 12, trackH);
    const ratio = total > view ? this.scroller.y / (total - view) : 0;
    this.thumb.style.height = `${thumbH}px`;
    this.thumb.style.transform = `translateY(${clamp(ratio, 0, 1) * (trackH - thumbH)}px)`;
    this.percent.textContent = `${Math.round(clamp(ratio, 0, 1) * 100)} %`;
  }

  leaveScene() {
    this.audio.sfx('back');
    this.app.back();
  }

  onButton(btn) {
    if (btn === 'B') return this.leaveScene();
    if (this.tab === 'repos' && (btn === 'UP' || btn === 'DOWN')) return this.moveRepo(btn === 'DOWN' ? 1 : -1);
    if (this.tab === 'repos' && btn === 'A') return this.openRepo();
    if (btn === 'UP' || btn === 'DOWN') return this.scroller.by(btn === 'DOWN' ? 40 : -40);
    if (btn === 'LEFT' || btn === 'RIGHT') return this.scroller.by(btn === 'RIGHT' ? 150 : -150);
    if (btn === 'L' || btn === 'R') {
      const i = this.tabs.indexOf(this.tab) + (btn === 'R' ? 1 : -1);
      if (i >= 0 && i < this.tabs.length) this.setTab(this.tabs[i]);
    }
  }

  onShow() {
    this.audio.muffle(0.4);
  }

  onLanguage() {
    this.tabBar.setOptions(this.tabs.map((id) => ({ value: id, label: t('gh.' + id) })));
    this.tabsSeg.seg.sync(false);
    this.renderTab(true);
  }
}
