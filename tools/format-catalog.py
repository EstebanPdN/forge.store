#!/usr/bin/env python3
"""Rewrites catalog/forge-store.json with a compact, readable layout:
small objects and arrays stay on one line, larger ones are expanded."""
import json
import pathlib
import sys

WIDTH = 118


def inline(value):
    """One-line JSON with a space inside braces, built without touching string contents."""
    if isinstance(value, dict):
        if not value:
            return '{}'
        return '{ ' + ', '.join(f'{json.dumps(k, ensure_ascii=False)}: {inline(v)}' for k, v in value.items()) + ' }'
    if isinstance(value, list):
        return '[' + ', '.join(inline(v) for v in value) + ']'
    return json.dumps(value, ensure_ascii=False)


def dump(value, indent=0):
    flat = inline(value)
    if not isinstance(value, (dict, list)) or len(flat) + indent <= WIDTH:
        return flat
    pad = ' ' * (indent + 2)
    if isinstance(value, dict):
        items = [f'{pad}{json.dumps(k, ensure_ascii=False)}: {dump(v, indent + 2)}' for k, v in value.items()]
        return '{\n' + ',\n'.join(items) + '\n' + ' ' * indent + '}'
    items = [pad + dump(v, indent + 2) for v in value]
    return '[\n' + ',\n'.join(items) + '\n' + ' ' * indent + ']'


def main(path):
    p = pathlib.Path(path)
    data = json.loads(p.read_text())
    p.write_text(dump(data) + '\n')


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else pathlib.Path(__file__).resolve().parent.parent / 'catalog' / 'forge-store.json')
