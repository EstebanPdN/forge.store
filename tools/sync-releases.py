#!/usr/bin/env python3
"""Moves the bundled catalog to the releases GitHub publishes now.

Uses the same rules as the store's live sync (3ds/source/live_release.c): the newest
non-draft release is stable, the newest pre-release is experimental only when it is
newer than stable, and asset names are re-tagged from the known ones. A deleted
pre-release therefore never stays in the catalog that ships with the store.

It also copies development percentages from the Progress page of my website
(https://estebanpdn.github.io/) into every app that names its card with "progressCard";
the store reads the same page once per launch (3ds/source/site_progress.h).

Finally it keeps what the store needs to recognize apps installed without it (FBI,
Universal-Updater; 3ds/source/detect.c): each release's CIA title and version, read from
the first bytes of its CIA, and the Universal-DB entry of the same repository (title IDs,
installed files and the name Universal-Updater records it under).

Usage: python3 tools/sync-releases.py [--check]
Needs an authenticated `gh`. Writes catalog/forge-store.json and 3ds/romfs/catalog.json.
"""
import json, pathlib, re, struct, subprocess, sys, urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent
CATALOG = ROOT / 'catalog' / 'forge-store.json'
BUNDLED = ROOT / '3ds' / 'romfs' / 'catalog.json'
TAG = re.compile(r'^[A-Za-z0-9._+-]{1,120}$')
GENERIC = {
    'es': ['Consulta el historial completo en GitHub.'],
    'en': ['Read the full changelog on GitHub.'],
    'pt': ['Veja o histórico completo no GitHub.'],
    'it': ['Leggi il changelog completo su GitHub.'],
    'fr': ['Consultez l’historique complet sur GitHub.'],
    'de': ['Den vollständigen Changelog gibt es auf GitHub.'],
    'ru': ['Полный список изменений — на GitHub.'],
    'ja': ['変更履歴の全文は GitHub をご覧ください。'],
}


SITE = 'https://estebanpdn.github.io/'
CARD = re.compile(r'class="progress-card ([^" ]+)[^>]*>(.*?)(?=class="progress-card |\Z)', re.S)


def site_progress():
    """card -> percentage, same rules as site_progress.h (first card wins, 0..100)."""
    req = urllib.request.Request(SITE, headers={'User-Agent': 'forge.store-catalog-sync'})
    html = urllib.request.urlopen(req, timeout=30).read().decode('utf-8', 'replace')
    out = {}
    for card, body in CARD.findall(html):
        m = re.search(r'aria-valuenow="(\d{1,3})"', body)
        if m and int(m.group(1)) <= 100 and card not in out:
            out[card] = int(m.group(1))
    return out


def releases(repo):
    out = subprocess.run(['gh', 'api', f'repos/{repo}/releases?per_page=12'], capture_output=True, text=True)
    if out.returncode:
        raise SystemExit(f'{repo}: {out.stderr.strip()}')
    return json.loads(out.stdout)


def retag(asset, live, old, tag, nested=False, single_cia=False):
    name = asset['name']
    if old and old in name:
        guess = name.replace(old, tag)
    else:
        guess = name.replace(old[1:] if old.startswith('v') else old, tag[1:] if tag.startswith('v') else tag)
    match = next((a for a in live['assets'] if a['name'] == guess), None) or \
        next((a for a in live['assets'] if a['name'] == name), None)
    if not match:
        candidates = [a for a in live['assets'] if a['name'].casefold() == guess.casefold()]
        if not candidates and single_cia and name.lower().endswith('.cia'):
            candidates = [a for a in live['assets'] if a['name'].lower().endswith('.cia')]
        if len(candidates) != 1:
            return None
        match = candidates[0]
    updated = dict(asset, name=match['name'], size=match['size'])
    if old != tag or name != match['name'] or asset.get('size') != match['size']:
        updated.pop('sha256', None)
    digest = match.get('digest') or ''
    if digest.startswith('sha256:') and len(digest) == 71:
        updated['sha256'] = digest[7:]
    if tag != old or updated['name'] != name or updated.get('sha256') != asset.get('sha256'):
        updated.pop('sumsName', None)
    if updated.get('url'):
        updated['url'] = match.get('browser_download_url') or updated['url'].replace(old, tag)
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
    if not isinstance(known, dict):
        return False
    assets = {}
    for key, asset in known.get('assets', {}).items():
        updated = retag(asset, live, known.get('tag', ''), tag, single_cia=key == 'cia' and 'o3ds' not in asset)
        if not updated:
            log.append(f'  {channel}: {tag} lacks a match for {asset["name"]}; kept {known.get("tag")}')
            return False
        assets[key] = updated
    if not assets:
        return False
    revision = max((a.get('updated_at', '') for a in live['assets']), default='')
    same = rels.get(channel, {}).get('tag') == tag
    if same and revision < known.get('assetUpdated', ''):
        return False
    if same and assets == known.get('assets') and revision == known.get('assetUpdated', '') and live['published_at'] == known.get('publishedAt'):
        return False
    updated = dict(known, tag=tag, date=live['published_at'][:10], publishedAt=live['published_at'], assets=assets)
    if revision:
        updated['assetUpdated'] = revision
    if not same or assets.get('cia') != known.get('assets', {}).get('cia'):
        updated.pop('ciaTitle', None)
        updated.pop('ciaVersion', None)
    if rels.get(channel, {}).get('tag') != tag:
        updated['notes'] = GENERIC
    rels[channel] = updated
    log.append(f'  {channel}: {known.get("tag")} -> {tag}')
    return True


UDB_FULL = 'https://db.universal-team.net/data/full.json'
UDB_STORE = 'https://db.universal-team.net/unistore/universal-db.unistore'
SIGNATURE = {0x10000: 0x23C, 0x10001: 0x13C, 0x10002: 0x7C, 0x10003: 0x23C, 0x10004: 0x13C, 0x10005: 0x7C}


def fetch(url, limit=None, headers=None):
    req = urllib.request.Request(url, headers={'User-Agent': 'forge.store-catalog-sync', **(headers or {})})
    with urllib.request.urlopen(req, timeout=60) as response:
        return response.read(limit) if limit else response.read()


def cia_identity(head):
    """(title ID, title version) from a CIA's first bytes (header, certificates, ticket, TMD)."""
    if len(head) < 0x20:
        return None
    header, _, _, certs, ticket = struct.unpack_from('<IHHII', head, 0)
    align = lambda n: (n + 63) & ~63
    tmd = align(header) + align(certs) + align(ticket)
    if tmd + 4 > len(head):
        return None
    signature = SIGNATURE.get(struct.unpack_from('>I', head, tmd)[0])
    body = tmd + 4 + (signature or 0)
    if not signature or body + 0x9E > len(head):
        return None
    title = struct.unpack_from('>Q', head, body + 0x4C)[0]
    version = struct.unpack_from('>H', head, body + 0x9C)[0]
    return (title, version) if title >> 32 == 0x00040000 else None


def udb_entries():
    """GitHub repository (lowercase) -> Universal-DB entry info, as Universal-Updater reads it."""
    full = json.loads(fetch(UDB_FULL))
    store = json.loads(fetch(UDB_STORE))
    infos = {e['info']['title']: e['info'] for e in store.get('storeContent', [])
             if isinstance(e, dict) and isinstance(e.get('info'), dict) and e['info'].get('title')}
    out = {}
    for item in full:
        repo = (item.get('github') or '').lower()
        info = infos.get(item.get('title'))
        if repo and info and '3DS' in (info.get('console') or []) and repo not in out:
            out[repo] = info
    return out


def sync_detection(data):
    """Keeps each app's "detect" data and each release's CIA identity current."""
    changed = False
    try:
        udb = udb_entries()
    except Exception as error:  # the next run tries again; the store keeps the last data
        print(f'Universal-DB unavailable ({error}); detection data kept')
        udb = None
    for app in data['apps']:
        rels = app.get('releases') or {}
        for channel, rel in rels.items():
            asset = (rel or {}).get('assets', {}).get('cia')
            if not app.get('repo') or not asset or not str(asset.get('name', '')).lower().endswith('.cia') or \
                    'ciaTitle' in rel:
                continue
            url = f'https://github.com/{app["repo"]}/releases/download/{rel["tag"]}/{asset["name"]}'
            try:
                identity = cia_identity(fetch(url, 65536, {'Range': 'bytes=0-65535'}))
            except Exception as error:
                print(app['id'], f'{channel}: CIA header unavailable ({error})')
                continue
            if identity:
                rel['ciaTitle'] = f'{identity[0]:016x}'
                rel['ciaVersion'] = identity[1]
                print(app['id'], f'{channel}: CIA {rel["ciaTitle"]} version {identity[1]}')
                changed = True
        ids, files, uu = [], [], None
        info = udb.get((app.get('repo') or '').lower()) if udb is not None else None
        if info:
            ids += [i for i in info.get('title_ids') or [] if isinstance(i, int) and not isinstance(i, bool)]
            files += [f for f in info.get('installed_files') or [] if isinstance(f, str)]
            uu = info.get('title')
        if app.get('repo'):
            files += [f for f in extracted_3dsx(app) if f not in files]
        for rel in rels.values():
            title = (rel or {}).get('ciaTitle')
            unique = (int(title, 16) >> 8) & 0xFFFFF if title else 0
            # 0xFF3FF is the devkitPro template's ID, shared by unrelated homebrew.
            if unique and unique != 0xFF3FF and unique not in ids:
                ids.append(unique)
        detect = {k: v for k, v in (('ids', ids), ('files', files), ('uu', uu)) if v}
        if udb is None and 'detect' in app:
            detect = {**app['detect'], **({'ids': ids} if ids else {})}
        if detect != app.get('detect', {}):
            if detect:
                app['detect'] = detect
            else:
                app.pop('detect', None)
            print(app['id'], f'detect: {detect}')
            changed = True
    return changed


BUILD_ID = re.compile(r'-[0-9a-f]{7,40}(?=\.)')


def plain(name):
    """An asset name without its build id: DK64Recompiled3D-3ds-2dde0a3.zip -> DK64Recompiled3D-3ds.zip."""
    return BUILD_ID.sub('', name)


def follow_pinned(app, live, log):
    """A pinned release its author keeps replacing under the same tag ("rolling"): follow the
    files it publishes now, matched by name without their build id, with their new hashes."""
    rel = (app.get('releases') or {}).get('stable')
    current = next((r for r in live if rel and r['tag_name'] == rel.get('tag')), None)
    if not current:
        return False
    renames = {}
    base = f'https://github.com/{app["repo"]}/releases/download/'
    for key, asset in rel.get('assets', {}).items():
        if asset.get('url') and not asset['url'].startswith(base):
            continue  # published elsewhere (e.g. a build server): not this release's file
        match = next((a for a in current['assets'] if a['name'] == asset['name']), None)
        if not match:
            same = [a for a in current['assets'] if plain(a['name']) == plain(asset['name'])]
            if len(same) != 1:
                log.append(f'  {key}: {asset["name"]} is gone and has no single successor; kept')
                return False
            match = same[0]
        digest = match.get('digest') or ''
        sha = digest[7:] if digest.startswith('sha256:') and len(digest) == 71 else None
        # Renamed, or replaced under the same name (its pinned hash no longer matches).
        replaced = bool(sha) and sha != asset.get('sha256')
        if match['name'] != asset['name'] or replaced:
            renames[asset['name']] = (match['name'], match['size'], sha)
    if not renames:
        return False
    for asset in rel['assets'].values():
        if asset['name'] in renames:
            old = asset['name']
            new, size, sha = renames[old]
            asset.update(name=new, size=size)
            asset.pop('sha256', None)
            if sha:
                asset['sha256'] = sha
            if asset.get('url'):
                asset['url'] = asset['url'].replace(old, new)
    rel['date'] = current['published_at'][:10]
    rel.pop('ciaTitle', None)
    rel.pop('ciaVersion', None)
    for steps in (app.get('install') or {}).values():
        for step in steps:
            for key, value in list(step.items()):
                if isinstance(value, str):
                    for old, (new, _, _) in renames.items():
                        value = value.replace(old, new)
                    step[key] = value
            if step.get('type') == 'downloadUrl':
                for new, size, sha in renames.values():
                    if step.get('name') == new or step.get('url', '').endswith('/' + new):
                        step['size'] = size
                        step.pop('sha256', None)
                        if sha:
                            step['sha256'] = sha
    log.append('  pinned ' + rel['tag'] + ': ' + ', '.join(f'{o} -> {n[0]}' for o, n in renames.items()))
    return True


def zip_names(url, size):
    """File names in a .zip, read from its central directory at the end (no full download)."""
    start = max(0, int(size) - 65536)
    tail = fetch(url, 65536, {'Range': f'bytes={start}-{int(size) - 1}'})
    end = tail.rfind(b'PK\x05\x06')
    if end < 0 or end + 22 > len(tail):
        return None
    size, offset = struct.unpack_from('<II', tail, end + 12)
    start = len(tail) - (len(tail) - end) - size
    if start < 0:
        return None
    names, at = [], start
    while at + 46 <= end and tail[at:at + 4] == b'PK\x01\x02':
        n, extra, comment = struct.unpack_from('<HHH', tail, at + 28)
        names.append(tail[at + 46:at + 46 + n].decode('utf-8', 'replace'))
        at += 46 + n + extra + comment
    return names


def extracted_3dsx(app):
    """Where the 3DSX plan's archives put their .3dsx files (install.c's extract() rules)."""
    steps = (app.get('install') or {}).get('3dsx') or []
    rel = (app.get('releases') or {}).get('stable') or {}
    paths = []
    for step in steps:
        if step.get('type') != 'extractFile' or str(step.get('output', '')).lower().endswith('.3dsx'):
            continue
        source = next((s for s in steps if s.get('output') == step.get('file') and
                       s.get('type') in ('downloadRelease', 'downloadUrl')), None)
        if not source or not str(step.get('file', '')).lower().endswith('.zip'):
            continue
        url, size = (source.get('url'), source.get('size')) if source['type'] == 'downloadUrl' else (None, None)
        if source['type'] == 'downloadRelease':
            asset = rel.get('assets', {}).get(source.get('asset')) or {}
            url = asset.get('name') and f'https://github.com/{app["repo"]}/releases/download/{rel["tag"]}/{asset["name"]}'
            size = asset.get('size')
        if not url or not isinstance(size, int) or size <= 0:
            continue
        try:
            names = zip_names(url, size)
        except Exception as error:
            print(app['id'], f'archive listing unavailable ({error})')
            continue
        pattern = step.get('input') or ''
        prefix = pattern[1:] if pattern.startswith('^') else pattern
        literal = prefix if not re.search(r'[\\.*+?\[\]()$|]', prefix) else ''
        out = step['output']
        for name in names or []:
            if not name.lower().endswith('.3dsx') or (pattern and not re.search(pattern, name)):
                continue
            if out.endswith('/'):
                paths.append(out + name)
            elif literal and name.startswith(literal + '/'):
                paths.append(out + '/' + name[len(literal) + 1:])
    return paths


def main():
    check = '--check' in sys.argv
    data = json.loads(CATALOG.read_text())
    changed = False
    for app in data['apps']:
        rels = app.get('releases')
        if not app.get('repo') or not rels:
            continue
        live = [r for r in releases(app['repo']) if not r['draft'] and TAG.match(r['tag_name'])
                and not r['tag_name'].startswith('store-media-')]
        if app.get('releasePinned'):
            log = []
            changed |= follow_pinned(app, live, log)
            if log:
                print(app['id'], app['repo'])
                print('\n'.join(log))
            continue
        live.sort(key=lambda r: r['published_at'], reverse=True)
        stable = next((r for r in live if not r['prerelease']), None)
        pre = next((r for r in live if r['prerelease']), None)
        log = []
        changed |= merge_channel(rels, 'stable', stable, log)
        stable_release = rels.get('stable', {})
        stable_date = stable_release.get('publishedAt') or stable_release.get('date', '')
        if pre and pre['published_at'] > stable_date:
            changed |= merge_channel(rels, 'experimental', pre, log)
        elif 'experimental' in rels and rels.get('stable') and \
                (rels['experimental'].get('publishedAt') or rels['experimental'].get('date', '')) <= stable_date:
            # The store hides an experimental build older than stable; so does the catalog.
            log.append(f'  experimental: {rels["experimental"]["tag"]} is older than stable; removed')
            del rels['experimental']
            changed = True
        if log:
            print(app['id'], app['repo'])
            print('\n'.join(log))
    progress = site_progress()
    for app in data['apps']:
        card = app.get('progressCard')
        if card in progress and app.get('progress') != progress[card]:
            print(app['id'], f'progress: {app.get("progress")} -> {progress[card]} ({card})')
            app['progress'] = progress[card]
            changed = True
        elif card and card not in progress:
            print(app['id'], f'progress card {card} not found on the website; kept {app.get("progress")}')
    changed |= sync_detection(data)
    if check:
        raise SystemExit(1 if changed else 0)
    if changed:
        CATALOG.write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n')
        BUNDLED.write_bytes(CATALOG.read_bytes())


if __name__ == '__main__':
    main()
