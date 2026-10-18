#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Renders the questions of docs/faq.md into site/faq.html, so the two never disagree (journal 0126).

docs/faq.md is the source: "## N. Question", paragraphs, and a last line "[sources]". Only the
part of site/faq.html between the first question and the closing reminder is replaced; the page's
head, header and footer stay as written. Run from the repository root:
    python3 scripts/site-faq.py          (rewrite site/faq.html)
    python3 scripts/site-faq.py --check  (exit 1 if site/faq.html is out of date)
"""
import html
import re
import sys
from pathlib import Path

md = Path('docs/faq.md').read_text()
page = Path('site/faq.html')
old = page.read_text()


def inline(text):
    out = html.escape(text, quote=False)
    out = re.sub(r'`([^`]+)`', r'<code>\1</code>', out)
    out = re.sub(r'\[([^\]]+)\]\((https?://[^)]+)\)', r'<a href="\2">\1</a>', out)
    return out


parts = []
for m in re.finditer(r'^## (\d+)\. (.+?)\n(.*?)(?=^## \d+\. |\Z)', md, re.S | re.M):
    num, title, body = m.group(1), m.group(2), m.group(3).strip()
    paras = [p.replace('\n', ' ') for p in re.split(r'\n\s*\n', body)]
    src = None
    last = paras[-1]
    k = last.rfind('[')
    if last.endswith(']') and k >= 0:
        src = last[k + 1:-1]
        paras[-1] = last[:k].strip()
    parts.append(f'<h2 id="q{num}">{num}. {inline(title)}</h2>')
    parts.extend(f'<p>{inline(p)}</p>' for p in paras if p)
    if src:
        parts.append(f'<p><small>Source: {inline(src)}</small></p>')
start = old.index('<h2 id="q1">')
end = old.index('<aside class="status">')
new = old[:start] + '\n'.join(parts) + '\n' + old[end:]
date = re.search(r'as of (\d{4}-\d{2}-\d{2})', md)
if date:
    new = re.sub(r'as of \d{4}-\d{2}-\d{2}', 'as of ' + date.group(1), new)
if '--check' in sys.argv:
    if new != old:
        print('site/faq.html is out of date: run python3 scripts/site-faq.py')
        sys.exit(1)
    print('site/faq.html matches docs/faq.md')
else:
    page.write_text(new)
    print(f'site/faq.html: {len(parts)} elements from docs/faq.md')
