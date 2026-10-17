#!/usr/bin/env python3
"""Check local Markdown links and GitHub heading anchors in README* and docs/."""
import html
from pathlib import Path
import re
import sys
import unicodedata
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]


def prose(text):
    """Hide fenced examples, preserving line numbers for diagnostics."""
    fence = None
    result = []
    for line in text.splitlines(keepends=True):
        marker = re.match(r'^ {0,3}(`{3,}|~{3,})', line)
        if fence is None and marker:
            fence = marker.group(1)
            result.append('\n' if line.endswith('\n') else '')
        elif fence is not None:
            if re.match(r'^ {0,3}' + re.escape(fence[0])
                        + '{' + str(len(fence)) + r',}\s*$', line):
                fence = None
            result.append('\n' if line.endswith('\n') else '')
        else:
            result.append(line)
    return ''.join(result)


def anchors(text):
    result, seen = set(), {}
    text = prose(text)
    for heading in re.findall(r'^ {0,3}#{1,6}\s+(.+?)\s*#*\s*$', text, re.M):
        heading = re.sub(r'!?\[([^]]+)\]\([^)]*\)', r'\1', heading)
        heading = html.unescape(re.sub(r'<[^>]*>', '', heading)).lower()
        slug = ''.join(char for char in heading
                       if char in (' ', '-', '_')
                       or unicodedata.category(char)[0] in 'LNM')
        slug = slug.replace(' ', '-')
        base = slug
        while slug in seen:
            seen[base] = seen.get(base, 0) + 1
            slug = f'{base}-{seen[base]}'
        seen[slug] = 0
        result.add(slug)
    result.update(re.findall(r'\b(?:id|name)=["\']([^"\']+)["\']', text))
    return result


def links(text):
    text = prose(text)
    # A Markdown code span cannot contain a link; retain its line breaks.
    text = re.sub(r'(`+)([\s\S]*?)\1',
                  lambda match: '\n' * match.group().count('\n'), text)
    definitions = {}
    for match in re.finditer(r'^ {0,3}\[([^]]+)\]:\s*(<[^>]+>|\S+)', text, re.M):
        definitions[match.group(1).casefold()] = match.group(2).strip('<>')
        yield text.count('\n', 0, match.start()) + 1, match.group(2).strip('<>')
    # Repository docs use inline links, including balanced parentheses in paths.
    pattern = r'!?\[([^]\n]*)\]\((<[^>]+>|(?:[^\s()]|\([^()]*\))+)(?:\s+["\'][^\n]*?["\'])?\)'
    for match in re.finditer(pattern, text):
        yield text.count('\n', 0, match.start()) + 1, match.group(2).strip('<>')
    for match in re.finditer(r'!?\[([^]\n]+)\]\[([^]\n]*)\]', text):
        label = (match.group(2) or match.group(1)).casefold()
        yield text.count('\n', 0, match.start()) + 1, definitions.get(label)


def check_link(source, target, root=ROOT):
    if target is None:
        return 'undefined reference link'
    url = urlsplit(target)
    if url.scheme or url.netloc:
        return None
    path = unquote(url.path)
    destination = (root / path.lstrip('/') if path.startswith('/')
                   else source.parent / path) if path else source
    destination = destination.resolve()
    if not destination.is_relative_to(root.resolve()):
        return 'relative link escapes the repository'
    if not destination.exists():
        return f'missing target: {target}'
    fragment = unquote(url.fragment)
    if fragment and destination.is_file():
        if destination.suffix.lower() == '.md':
            if fragment not in anchors(destination.read_text()):
                return f'missing heading anchor: {target}'
        elif re.fullmatch(r'L\d+(?:-L\d+)?', fragment):
            numbers = [int(value) for value in re.findall(r'\d+', fragment)]
            if min(numbers) < 1 or max(numbers) > len(destination.read_text().splitlines()):
                return f'invalid source line anchor: {target}'
        else:
            return f'unsupported local anchor: {target}'
    return None


def main():
    sources = sorted(set(ROOT.glob('README*.md')) | set((ROOT / 'docs').rglob('*.md')))
    failures, count = [], 0
    for source in sources:
        for line, target in links(source.read_text()):
            count += 1
            problem = check_link(source, target)
            if problem:
                failures.append(f'{source.relative_to(ROOT)}:{line}: {problem}')
    if failures:
        print('\n'.join(failures), file=sys.stderr)
        return 1
    print(f'Checked {count} links in {len(sources)} Markdown files; local targets and anchors are valid.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
