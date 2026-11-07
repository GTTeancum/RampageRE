import re
def masked(source):
    # Mask comments and literals, retaining positions/newlines for brace balancing.
    token = re.compile(r'/\*[\s\S]*?\*/|//[^\n]*|"(?:\\[\s\S]|[^"\\])*"|\'(?:\\[\s\S]|[^\'\\])*\'')
    return token.sub(lambda m: ''.join('\n' if c == '\n' else ' ' for c in m[0]), source)


def extract(source, name):
    clean = masked(source)
    pattern = r'^(?:static\s+)?[\w *]+\b' + re.escape(name) + r'\([^;{}]*\)\s*\{'
    matches = list(re.finditer(pattern, clean, re.M))
    if len(matches) != 1:
        raise ValueError(f'{name}: expected one definition, found {len(matches)}')
    start, end = matches[0].span()
    depth = 1
    while depth and end < len(clean):
        depth += (clean[end] == '{') - (clean[end] == '}')
        end += 1
    if depth:
        raise ValueError(f'{name}: unterminated function')
    return source[start:end], start, end

