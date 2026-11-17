#!/usr/bin/env python3
"""Moves the bundled catalog to the releases GitHub publishes now.

Uses the same rules as the store's live sync (3ds/source/live_release.c): the newest
non-draft release is stable, the newest pre-release is experimental only when it is
newer than stable, and asset names are re-tagged from the known ones. A deleted
pre-release therefore never stays in the catalog that ships with the store.

Usage: python3 tools/sync-releases.py [--check]
Needs an authenticated `gh`. Writes catalog/forge-store.json and 3ds/romfs/catalog.json.
"""
import json, pathlib, re, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
CATALOG = ROOT / 'catalog' / 'forge-store.json'
BUNDLED = ROOT / '3ds' / 'romfs' / 'catalog.json'
TAG = re.compile(r'^[A-Za-z0-9._+-]{1,120}$')
GENERIC = {'es': ['Consulta el historial completo en GitHub.'],
           'en': ['Read the full changelog on GitHub.']}


def releases(repo):
    out = subprocess.run(['gh', 'api', f'repos/{repo}/releases?per_page=12'], capture_output=True, text=True)
    if out.returncode:
        raise SystemExit(f'{repo}: {out.stderr.strip()}')
    return json.loads(out.stdout)


def retag(asset, live, old, tag, nested=False):
    name = asset['name']
    if old and old in name:
        guess = name.replace(old, tag)
    else:
        guess = name.replace(old[1:] if old.startswith('v') else old, tag[1:] if tag.startswith('v') else tag)
    match = next((a for a in live['assets'] if a['name'] == guess), None) or \
        next((a for a in live['assets'] if a['name'] == name), None)
    if not match:
        return None
    updated = dict(asset, name=match['name'], size=match['size'])
    updated.pop('sha256', None)
    digest = match.get('digest') or ''
    if digest.startswith('sha256:') and len(digest) == 71:
        updated['sha256'] = digest[7:]
    if updated.get('url'):
        updated['url'] = updated['url'].replace(old, tag)
    if 'o3ds' in asset:
        if nested:
            return None
        mapped = retag(asset['o3ds'], live, old, tag, True)
        if not mapped:
            return None
        updated['o3ds'] = mapped
    return updated


def merge_channel(rels, channel, live, log):
    if not live or not TAG.match(live['tag_name']):
        return False
    known = rels.get(channel) or rels.get('stable') or rels.get('experimental')
    tag = live['tag_name']
    if not isinstance(known, dict) or known.get('tag') == tag:
        return False
    assets = {}
    for key, asset in known.get('assets', {}).items():
        updated = retag(asset, live, known.get('tag', ''), tag)
        if not updated:
            log.append(f'  {channel}: {tag} lacks a match for {asset["name"]}; kept {known.get("tag")}')
            return False
        assets[key] = updated
    if not assets:
        return False
    updated = dict(known, tag=tag, date=live['published_at'][:10], assets=assets)
    if rels.get(channel, {}).get('tag') != tag:
        updated['notes'] = GENERIC
    rels[channel] = updated
    log.append(f'  {channel}: {known.get("tag")} -> {tag}')
    return True


def main():
    check = '--check' in sys.argv
    data = json.loads(CATALOG.read_text())
    changed = False
    for app in data['apps']:
        rels = app.get('releases')
        if not app.get('repo') or not rels or app.get('releasePinned'):
            continue
        live = [r for r in releases(app['repo']) if not r['draft'] and TAG.match(r['tag_name'])
                and not r['tag_name'].startswith('store-media-')]
        stable = next((r for r in live if not r['prerelease']), None)
        pre = next((r for r in live if r['prerelease']), None)
        log = []
        changed |= merge_channel(rels, 'stable', stable, log)
        stable_date = rels.get('stable', {}).get('date', '')
        if pre and pre['published_at'][:10] > stable_date:
            changed |= merge_channel(rels, 'experimental', pre, log)
        elif 'experimental' in rels and rels.get('stable') and \
                rels['experimental'].get('date', '') <= stable_date:
            # The store hides an experimental build older than stable; so does the catalog.
            log.append(f'  experimental: {rels["experimental"]["tag"]} is older than stable; removed')
            del rels['experimental']
            changed = True
        if log:
            print(app['id'], app['repo'])
            print('\n'.join(log))
    if check:
        raise SystemExit(1 if changed else 0)
    if changed:
        CATALOG.write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n')
        BUNDLED.write_bytes(CATALOG.read_bytes())


if __name__ == '__main__':
    main()
