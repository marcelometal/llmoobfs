#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Check static HTML links and fragments without network dependencies."""
from datetime import datetime, timezone
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1] / 'site'


class Page(HTMLParser):
    def __init__(self, path):
        super().__init__()
        self.ids = set()
        self.links = []
        self.lang = None
        self.counts = {}
        self.problems = []
        self.feed(path.read_text())

    def handle_starttag(self, tag, attrs):
        data = dict(attrs)
        self.counts[tag] = self.counts.get(tag, 0) + 1
        if tag == 'html':
            self.lang = data.get('lang')
        # accessibility (issue #12): images need alt text; an SVG is either decorative or named
        if tag == 'img' and 'alt' not in data:
            self.problems.append(f"img without alt: {data.get('src')}")
        if tag == 'svg' and data.get('aria-hidden') != 'true' and not (
                data.get('role') == 'img' and (data.get('aria-label') or data.get('aria-labelledby'))):
            self.problems.append('svg neither aria-hidden nor role="img" with a name')
        if 'id' in data:
            assert data['id'] not in self.ids, f"Duplicate ID: {data['id']}"
            self.ids.add(data['id'])
        for attribute in ('href', 'src'):
            if attribute in data:
                self.links.append(data[attribute])


pages = {path: Page(path) for path in ROOT.rglob('*.html')}
assert pages, 'No site pages found'
for source, page in pages.items():
    for link in page.links:
        parsed = urlsplit(link)
        if parsed.scheme or parsed.netloc:
            continue
        assert not parsed.path.startswith('/'), f'Root-relative link breaks project Pages: {link}'
        target = (source.parent / unquote(parsed.path)).resolve() if parsed.path else source
        assert target.is_relative_to(ROOT), f'Link outside site: {link}'
        assert target.exists(), f'{source.name}: missing {link}'
        if parsed.fragment:
            assert target in pages and unquote(parsed.fragment) in pages[target].ids, f'{source.name}: missing anchor {link}'
for source, page in pages.items():
    name = source.relative_to(ROOT)
    assert page.lang, f'{name}: <html> without lang'
    assert page.counts.get('h1') == 1, f"{name}: {page.counts.get('h1', 0)} h1 elements, expected 1"
    for landmark in ('header', 'main', 'nav'):
        assert page.counts.get(landmark), f'{name}: no <{landmark}>'
    assert 'class="skip"' in source.read_text(), f'{name}: no skip link'
    assert not page.problems, f'{name}: ' + '; '.join(page.problems)
# security.txt (RFC 9116): Contact and a single Expires are required, and an expired file is
# invalid, so this check fails once the date passes until someone renews it
fields = {}
for line in (ROOT / '.well-known' / 'security.txt').read_text().splitlines():
    if line and not line.startswith('#'):
        key, _, value = line.partition(':')
        fields.setdefault(key.strip().lower(), []).append(value.strip())
assert fields.get('contact'), 'security.txt: no Contact'
assert len(fields.get('expires', [])) == 1, 'security.txt: Expires must appear once'
expires = datetime.fromisoformat(fields['expires'][0].replace('Z', '+00:00'))
assert expires > datetime.now(timezone.utc), f'security.txt: expired on {expires:%Y-%m-%d}, renew it'
print(f'Site links valid: {len(pages)} HTML pages; lang, one h1, landmarks, skip link, alt text checked;'
      f' security.txt valid until {expires:%Y-%m-%d}')
