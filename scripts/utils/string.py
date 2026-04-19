
HEX_DIGITS = set("0123456789abcdefABCDEF")

def decode_byte(s):
  # Return integer codepoint for a bound specification (e.g. '0x30', '\x30', '3')
  if s is None:
    raise ValueError("None bound")
  if isinstance(s, int):
    return s
  if not isinstance(s, str):
    try:
      return int(s)
    except Exception:
      raise
  if s.startswith('\\x') or s.startswith('\\X'):
    return int(s[2:4], 16)
  if s.startswith('0x') or s.startswith('0X'):
    h = ''.join(c for c in s[2:] if c in HEX_DIGITS)
    return int(h, 16)
  if s.startswith('\\') and len(s) >= 2:
    esc = s[1]
    if esc == 'n': return ord('\n')
    if esc == 't': return ord('\t')
    if esc == 'r': return ord('\r')
    if esc == 'b': return ord('\b')
    if esc == 'f': return ord('\f')
    if esc == '\\': return ord('\\')
    return ord(esc)
  if len(s) == 1:
    return ord(s)
  # fallback: try int
  return int(s)


def unescape_token(tok):
  if not tok:
    return tok
  # \xNN style
  if tok.startswith("\\x") or tok.startswith("\\X"):
    try:
      return chr(int(tok[2:4], 16))
    except Exception:
      return tok
  # 0xNN style
  if tok.startswith("0x") or tok.startswith("0X"):
    try:
      # consume hex digits
      h = ''.join(c for c in tok[2:] if c in HEX_DIGITS)
      return chr(int(h, 16))
    except Exception:
      return tok
  # simple escapes
  if tok.startswith('\\') and len(tok) >= 2:
    esc = tok[1]
    if esc == 'n':
      return '\n'
    if esc == 't':
      return '\t'
    if esc == 'r':
      return '\r'
    if esc == 'b':
      return '\b'
    if esc == 'f':
      return '\f'
    if esc == '\\':
      return '\\'
    return esc
  return tok
