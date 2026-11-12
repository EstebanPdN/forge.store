// SD card explorer: browse the virtual card the installer writes to. The top
// screen shows capacity and who created the selected file or folder.
import { Scene } from './scene.js';
import { h, clamp } from '../core/dom.js';
import { t, fmtSize, fmtDate } from '../core/i18n.js';
import { icon } from '../core/icons.js';
import { appIcon, Scroller, sdMeter } from '../ui/components.js';
import { parentOf, normalize } from '../core/sd.js';

export class SDScene extends Scene {
  static section = 'sd';

  build() {
    this.path = '/';
    this.focus = 0;
    this.detail = h('div');
    this.meterEl = h('div');
    this.top.append(h('div.bg-forge'), h('div.pn-top', h('h2.pn-title', h('b', t('sd.title'))), this.detail), this.meterEl);

    this.crumb = h('div.pn-bar-title.sd-crumb');
    this.note = h('span.pn-bar-note');
    this.list = h('div.scroll-content.pn-list');
    this.scrollEl = h('div.pn-scroll.scroller', this.list);
    this.scroller = new Scroller(this.scrollEl, this.list);
    this.bottom.append(
      h('div.bg-forge'),
      h('div.pn-bar', h('button.dt-back', { type: 'button', 'aria-label': t('action.back'), on: { click: () => this.up() } }, icon('back')), this.crumb, this.note),
      this.scrollEl,
    );
    this.render();
    this.listen(this.app.sd, 'change', () => this.render(true));
  }

  entries() {
    return this.app.sd.list(this.path);
  }

  render(keep = false) {
    if (!this.app.sd.get(this.path)) this.path = '/';
    this.items = this.entries();
    this.focus = clamp(keep ? this.focus : 0, 0, Math.max(0, this.items.length - 1));
    // Long paths keep their last folders: sdmc:/…/Zelda 3DS/mods
    const parts = this.path.split('/').filter(Boolean);
    this.crumb.textContent = parts.length > 2 ? `sdmc:/…/${parts.slice(-2).join('/')}` : 'sdmc:' + this.path;
    this.note.textContent = this.items.length === 1 ? t('sd.item') : t('sd.items', { n: this.items.length });
    this.rowEls = this.items.map((entry, i) => this.row(entry, i));
    this.list.replaceChildren(...(this.rowEls.length ? this.rowEls : [h('div.pn-empty', icon('folder'), t('sd.empty'))]));
    if (!keep) this.scroller.reset();
    requestAnimationFrame(() => this.scroller.syncBar());
    this.renderTop();
  }

  owner(node) {
    return node.by && node.by !== 'store' ? this.catalog.get(node.by) : null;
  }

  colour(node) {
    if (node.kind === 'user') return 'var(--mint)';
    if (node.by === 'store' || node.kind === 'store') return '#e7a008';
    if (this.owner(node)) return 'var(--forge)';
    if (node.kind === 'temp') return 'var(--ink-faint)';
    return node.type === 'dir' ? '#e7a008' : 'var(--ink-faint)';
  }

  row(entry, i) {
    const { node, name } = entry;
    const owner = this.owner(node);
    const size = node.type === 'dir' ? (node.fixed ? fmtSize(node.fixed) : t('sd.items', { n: Object.keys(node.children).length })) : fmtSize(node.size);
    return h(
      'button.li' + (i === this.focus ? '.focus' : ''),
      { type: 'button', on: { click: () => (i === this.focus ? this.open() : this.setFocus(i)) } },
      h('span.li-icon', { style: { '--c': this.colour(node) } }, icon(node.type === 'dir' ? (node.fixed ? 'lock' : 'folder') : name.endsWith('.3dsx') || name.endsWith('.cia') ? 'cart' : 'file')),
      h('span.li-text', h('b', name + (node.type === 'dir' ? '/' : '')), h('small', size)),
      owner ? appIcon(owner, { size: 16, cls: 'owner' }) : null,
    );
  }

  renderTop() {
    const entry = this.items[this.focus];
    if (!entry) {
      this.detail.replaceChildren(h('div.sd-detail', h('span.big', icon('folder')), h('div', h('b', 'sdmc:' + this.path), h('code', t('sd.empty')))));
    } else {
      const { node, name, path } = entry;
      const owner = this.owner(node);
      const tags = [];
      if (owner) tags.push(h('span.chip.forge', t('sd.createdBy', { app: owner.title })));
      else if (node.kind === 'user') tags.push(h('span.chip.mint', icon('check'), t('sd.userFile')));
      else if (node.by === 'store' || node.kind === 'store') tags.push(h('span.chip.amber', 'forge.store'));
      else if (node.kind === 'system') tags.push(h('span.chip', icon('lock'), t('sd.system')));
      tags.push(h('span.chip', fmtSize(this.app.sd.size(node))));
      if (node.date) tags.push(h('span.chip', fmtDate(node.date)));
      this.detail.replaceChildren(
        h(
          'div.sd-detail.pn-card',
          h('span.big', { style: { '--c': this.colour(node) } }, owner ? appIcon(owner, { size: 32 }) : icon(node.type === 'dir' ? 'folder' : 'file')),
          h('div', { style: { minWidth: 0 } }, h('b.ellipsis', name), h('code', 'sdmc:' + path), h('div.tags', tags)),
        ),
      );
    }
    this.meterEl.replaceChildren(sdMeter(this.app.sd));
  }

  setFocus(i) {
    if (!this.rowEls.length) return;
    i = clamp(i, 0, this.rowEls.length - 1);
    if (i === this.focus) return;
    this.rowEls[this.focus]?.classList.remove('focus');
    this.focus = i;
    this.rowEls[i].classList.add('focus');
    this.scroller.reveal(this.rowEls[i], 6);
    this.audio.sfx('move');
    this.renderTop();
  }

  open() {
    const entry = this.items[this.focus];
    if (!entry) return;
    if (entry.node.type !== 'dir') return this.audio.sfx('move', { pitch: -4 });
    if (entry.node.fixed) {
      this.audio.sfx('error');
      return this.app.toast({ text: t('sd.locked'), icon: 'lock', tone: 'amber' });
    }
    this.audio.sfx('select');
    this.path = normalize(entry.path);
    this.render();
  }

  up() {
    if (this.path === '/') {
      this.audio.sfx('back');
      return this.app.back();
    }
    const from = this.path;
    this.path = parentOf(this.path);
    this.audio.sfx('back');
    this.render();
    const i = this.items.findIndex((e) => e.path === from);
    if (i >= 0) {
      this.focus = 0;
      this.setFocus(i);
    }
  }

  remove() {
    const entry = this.items[this.focus];
    if (!entry) return;
    const { node, name, path } = entry;
    if (node.kind === 'system' || node.fixed || node.by === 'store') {
      this.audio.sfx('error');
      return this.app.toast({ text: t('sd.cantDelete'), icon: 'lock', tone: 'amber' });
    }
    this.app.sd.remove(path);
    this.state.save(this.app.sd);
    this.audio.sfx('back');
    this.app.toast({ text: t('toast.deleted', { name }), icon: 'trash', tone: 'danger' });
  }

  onButton(btn) {
    if (btn === 'UP' || btn === 'DOWN') return this.setFocus(this.focus + (btn === 'DOWN' ? 1 : -1));
    if (btn === 'A' || btn === 'RIGHT') return this.open();
    if (btn === 'B' || btn === 'LEFT') return this.up();
    if (btn === 'X') return this.remove();
    if (btn === 'SELECT') {
      this.audio.sfx('back');
      return this.app.back();
    }
  }

  onLanguage() {
    this.top.querySelector('.pn-title b').textContent = t('sd.title');
    this.render(true);
  }
}
