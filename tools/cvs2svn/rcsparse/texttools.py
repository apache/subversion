#
# Copyright (C) 2000-2002 The ViewCVS Group. All Rights Reserved.
#
# By using this file, you agree to the terms and conditions set forth in
# the LICENSE.html file which can be found at the top level of the ViewCVS
# distribution or at http://viewcvs.sourceforge.net/license-1.html.
#
# Contact information:
#   Greg Stein, PO Box 760, Palo Alto, CA, 94302
#   gstein@lyra.org, http://viewcvs.sourceforge.net/
#
# -----------------------------------------------------------------------
#
# This software is being maintained as part of the ViewCVS project.
# Information is available at:
#    http://viewcvs.sourceforge.net/
#
# -----------------------------------------------------------------------

import string

# note: this will raise an ImportError if it isn't available. the rcsparse
# package will recognize this and switch over to the default parser.
from mx import TextTools

import common


# for convenience
_tt = TextTools

_idchar_list = map(chr, range(33, 127)) + map(chr, range(160, 256))
_idchar_list.remove('$')
_idchar_list.remove(',')
#_idchar_list.remove('.')	leave as part of 'num' symbol
_idchar_list.remove(':')
_idchar_list.remove(';')
_idchar_list.remove('@')
_idchar = string.join(_idchar_list, '')
_idchar_set = _tt.set(_idchar)

_onechar_token_set = _tt.set(':;')

_not_at_set = _tt.invset('@')

_T_TOKEN = 30
_T_STRING_START = 40
_T_STRING_SPAN = 60
_T_STRING_END = 70

_E_COMPLETE = 100	# ended on a complete token
_E_TOKEN = 110	# ended mid-token
_E_STRING_SPAN = 130	# ended within a string
_E_STRING_END = 140	# ended with string-end ('@') (could be mid-@@)

_SUCCESS = +100

_EOF = 'EOF'
_CONTINUE = 'CONTINUE'
_UNUSED = 'UNUSED'


# continuation of a token over a chunk boundary
_c_token_table = (
  (_T_TOKEN,      _tt.AllInSet, _idchar_set),
  )

class _mxTokenStream:

  # the algorithm is about the same speed for any CHUNK_SIZE chosen.
  # grab a good-sized chunk, but not too large to overwhelm memory.
  # note: we use a multiple of a standard block size
  CHUNK_SIZE  = 192 * 512  # about 100k

#  CHUNK_SIZE  = 5	# for debugging, make the function grind...

  def __init__(self, file):
    self.rcsfile = file
    self.tokens = [ ]
    self.partial = None

    self.string_end = None

  def _parse_chunk(self, buf, start=0):
    "Get the next token from the RCS file."

    buflen = len(buf)

    assert start < buflen

    # construct a tag table which refers to the buffer we need to parse.
    table = (
      # ignore whitespace. with or without whitespace, move to the next rule.
      (None, _tt.AllInSet, _tt.whitespace_set, +1),

      (_E_COMPLETE, _tt.EOF + _tt.AppendTagobj, _tt.Here, +1, _SUCCESS),

      # accumulate token text and exit, or move to the next rule.
      (_UNUSED,      _tt.AllInSet + _tt.AppendMatch, _idchar_set, +2),

      (_E_TOKEN,  _tt.EOF + _tt.AppendTagobj, _tt.Here, -3, _SUCCESS),

      # single character tokens exit immediately, or move to the next rule
      (_UNUSED,    _tt.IsInSet + _tt.AppendMatch, _onechar_token_set, +2),

      (_E_COMPLETE, _tt.EOF + _tt.AppendTagobj, _tt.Here, -5, _SUCCESS),

      # if this isn't an '@' symbol, then we have a syntax error (go to a
      # negative index to indicate that condition). otherwise, suck it up
      # and move to the next rule.
      (_T_STRING_START, _tt.Is + _tt.AppendTagobj, '@'),

      (None, _tt.Is, '@', +4, +1),
      (buf, _tt.Is, '@', +1, -1),
      (_T_STRING_END, _tt.Skip + _tt.AppendTagobj, 0, 0, +1),
      (_E_STRING_END, _tt.EOF + _tt.AppendTagobj, _tt.Here, -10, _SUCCESS),

      (_E_STRING_SPAN, _tt.EOF + _tt.AppendTagobj, _tt.Here, +1, _SUCCESS),

      # suck up everything that isn't an AT. go to next rule to look for EOF
      (buf,  _tt.AllInSet, _not_at_set, 0, +1),

      # go back to look for double AT if we aren't at the end of the string
      (_E_STRING_SPAN,   _tt.EOF + _tt.AppendTagobj, _tt.Here, -6, _SUCCESS),
      )

    success, taglist, idx = _tt.tag(buf, table, start)

    if not success:
      ### need a better way to report this error
      raise common.RCSIllegalCharacter()
    assert idx == buflen

    # pop off the last item
    last_which = taglist.pop()

    i = 0
    tlen = len(taglist)
    while i < tlen:
      if taglist[i] == _T_STRING_START:
        j = i + 1
        while j < tlen:
          if taglist[j] == _T_STRING_END:
            s = _tt.join(taglist, '', i+1, j)
            del taglist[i:j]
            tlen = len(taglist)
            taglist[i] = s
            break
          j = j + 1
        else:
          assert last_which == _E_STRING_SPAN
          s = _tt.join(taglist, '', i+1)
          del taglist[i:]
          self.partial = (_T_STRING_SPAN, [ s ])
          break
      i = i + 1

    # figure out whether we have a partial last-token
    if last_which == _E_TOKEN:
      self.partial = (_T_TOKEN, [ taglist.pop() ])
    elif last_which == _E_COMPLETE:
      pass
    elif last_which == _E_STRING_SPAN:
      assert self.partial
    else:
      assert last_which == _E_STRING_END
      self.partial = (_T_STRING_END, [ taglist.pop() ])

    taglist.reverse()
    taglist.extend(self.tokens)
    self.tokens = taglist

  def _set_end(self, taglist, text, l, r, subtags):
    self.string_end = l

  def _handle_partial(self, buf):
    which, chunks = self.partial
    if which == _T_TOKEN:
      success, taglist, idx = _tt.tag(buf, _c_token_table)
      if not success:
        # The start of this buffer was not a token. So the end of the
        # prior buffer was a complete token.
        self.tokens.insert(0, string.join(chunks, ''))
      else:
        assert len(taglist) == 1 and taglist[0][0] == _T_TOKEN \
               and taglist[0][1] == 0 and taglist[0][2] == idx
        if idx == len(buf):
          #
          # The whole buffer was one huge token, so we may have a
          # partial token again.
          #
          # Note: this modifies the list of chunks in self.partial
          #
          chunks.append(buf)

          # consumed the whole buffer
          return len(buf)

        # got the rest of the token.
        chunks.append(buf[:idx])
        self.tokens.insert(0, string.join(chunks, ''))

      # no more partial token
      self.partial = None

      return idx

    if which == _T_STRING_END:
      if buf[0] != '@':
        self.tokens.insert(0, string.join(chunks, ''))
        return 0
      chunks.append('@')
      start = 1
    else:
      start = 0

    self.string_end = None
    string_table = (
      (None,    _tt.Is, '@', +3, +1),
      (_UNUSED, _tt.Is + _tt.AppendMatch, '@', +1, -1),
      (self._set_end, _tt.Skip + _tt.CallTag, 0, 0, _SUCCESS),

      (None,    _tt.EOF, _tt.Here, +1, _SUCCESS),

      # suck up everything that isn't an AT. move to next rule to look
      # for EOF
      (_UNUSED, _tt.AllInSet + _tt.AppendMatch, _not_at_set, 0, +1),

      # go back to look for double AT if we aren't at the end of the string
      (None,    _tt.EOF, _tt.Here, -5, _SUCCESS),
      )

    success, unused, idx = _tt.tag(buf, string_table,
                                   start, len(buf), chunks)

    # must have matched at least one item
    assert success

    if self.string_end is None:
      assert idx == len(buf)
      self.partial = (_T_STRING_SPAN, chunks)
    elif self.string_end < len(buf):
      self.partial = None
      self.tokens.insert(0, string.join(chunks, ''))
    else:
      self.partial = (_T_STRING_END, chunks)

    return idx

  def _parse_more(self):
    buf = self.rcsfile.read(self.CHUNK_SIZE)
    if not buf:
      return _EOF

    if self.partial:
      idx = self._handle_partial(buf)
      if idx is None:
        return _CONTINUE
      if idx < len(buf):
        self._parse_chunk(buf, idx)
    else:
      self._parse_chunk(buf)

    return _CONTINUE

  def get(self):
    try:
      return self.tokens.pop()
    except IndexError:
      pass

    while not self.tokens:
      action = self._parse_more()
      if action == _EOF:
        return None

    return self.tokens.pop()


#  _get = get
#  def get(self):
    token = self._get()
    print 'T:', `token`
    return token

  def match(self, match):
    if self.tokens:
      token = self.tokens.pop()
      if token != match:
        raise RuntimeError, ('Unexpected parsing error in RCS file.\n'
                             'Expected token: %s, but saw: %s'
                             % (match, token))
    else:
      token = self.get()
      if token != match:
        raise RuntimeError, ('Unexpected parsing error in RCS file.\n'
                             'Expected token: %s, but saw: %s'
                             % (match, token))

  def unget(self, token):
    self.tokens.append(token)

  def mget(self, count):
    "Return multiple tokens. 'next' is at the end."
    while len(self.tokens) < count:
      action = self._parse_more()
      if action == _EOF:
        ### fix this
        raise RuntimeError, 'EOF hit while expecting tokens'
    result = self.tokens[-count:]
    del self.tokens[-count:]
    return result


class Parser(common._Parser):
  stream_class = _mxTokenStream
