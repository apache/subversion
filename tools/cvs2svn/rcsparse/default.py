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
# This file was originally based on portions of the blame.py script by
# Curt Hagenlocher.
#
# -----------------------------------------------------------------------

import string

import common


class _TokenStream:
  token_term = string.whitespace + ';'

  # the algorithm is about the same speed for any CHUNK_SIZE chosen.
  # grab a good-sized chunk, but not too large to overwhelm memory.
  # note: we use a multiple of a standard block size
  CHUNK_SIZE  = 192 * 512  # about 100k

#  CHUNK_SIZE  = 5	# for debugging, make the function grind...

  def __init__(self, file):
    self.rcsfile = file
    self.idx = 0
    self.buf = self.rcsfile.read(self.CHUNK_SIZE)
    if self.buf == '':
      raise RuntimeError, 'EOF'

  def get(self):
    "Get the next token from the RCS file."

    # Note: we can afford to loop within Python, examining individual
    # characters. For the whitespace and tokens, the number of iterations
    # is typically quite small. Thus, a simple iterative loop will beat
    # out more complex solutions.

    buf = self.buf
    idx = self.idx

    while 1:
      if idx == len(buf):
        buf = self.rcsfile.read(self.CHUNK_SIZE)
        if buf == '':
          # signal EOF by returning None as the token
          del self.buf	# so we fail if get() is called again
          return None
        idx = 0

      if buf[idx] not in string.whitespace:
        break

      idx = idx + 1

    if buf[idx] == ';':
      self.buf = buf
      self.idx = idx + 1
      return ';'

    if buf[idx] != '@':
      end = idx + 1
      token = ''
      while 1:
        # find token characters in the current buffer
        while end < len(buf) and buf[end] not in self.token_term:
          end = end + 1
        token = token + buf[idx:end]

        if end < len(buf):
          # we stopped before the end, so we have a full token
          idx = end
          break

        # we stopped at the end of the buffer, so we may have a partial token
        buf = self.rcsfile.read(self.CHUNK_SIZE)
        idx = end = 0

      self.buf = buf
      self.idx = idx
      return token

    # a "string" which starts with the "@" character. we'll skip it when we
    # search for content.
    idx = idx + 1

    chunks = [ ]

    while 1:
      if idx == len(buf):
        idx = 0
        buf = self.rcsfile.read(self.CHUNK_SIZE)
        if buf == '':
          raise RuntimeError, 'EOF'
      i = string.find(buf, '@', idx)
      if i == -1:
        chunks.append(buf[idx:])
        idx = len(buf)
        continue
      if i == len(buf) - 1:
        chunks.append(buf[idx:i])
        idx = 0
        buf = '@' + self.rcsfile.read(self.CHUNK_SIZE)
        if buf == '@':
          raise RuntimeError, 'EOF'
        continue
      if buf[i + 1] == '@':
        chunks.append(buf[idx:i+1])
        idx = i + 2
        continue

      chunks.append(buf[idx:i])

      self.buf = buf
      self.idx = i + 1

      return string.join(chunks, '')

#  _get = get
#  def get(self):
    token = self._get()
    print 'T:', `token`
    return token

  def match(self, match):
    "Try to match the next token from the input buffer."

    token = self.get()
    if token != match:
      raise RuntimeError, ('Unexpected parsing error in RCS file.\n' +
                           'Expected token: %s, but saw: %s' % (match, token))

  def unget(self, token):
    "Put this token back, for the next get() to return."

    # Override the class' .get method with a function which clears the
    # overridden method then returns the pushed token. Since this function
    # will not be looked up via the class mechanism, it should be a "normal"
    # function, meaning it won't have "self" automatically inserted.
    # Therefore, we need to pass both self and the token thru via defaults.

    # note: we don't put this into the input buffer because it may have been
    # @-unescaped already.

    def give_it_back(self=self, token=token):
      del self.get
      return token

    self.get = give_it_back

  def mget(self, count):
    "Return multiple tokens. 'next' is at the end."
    result = [ ]
    for i in range(count):
      result.append(self.get())
    result.reverse()
    return result


class Parser(common._Parser):
  stream_class = _TokenStream

  def parse_rcs_admin(self):
    while 1:
      # Read initial token at beginning of line
      token = self.ts.get()

      # We're done once we reach the description of the RCS tree
      if token[0] in string.digits:
        self.ts.unget(token)
        return

      if token == "head":
        semi, rev = self.ts.mget(2)
        self.sink.set_head_revision(rev)
        if semi != ';':
          raise common.RCSExpected(semi, ';')
      elif token == "branch":
        semi, branch = self.ts.mget(2)
        self.sink.set_principal_branch(branch)
        if semi != ';':
          raise common.RCSExpected(semi, ';')
      elif token == "symbols":
        while 1:
          tag = self.ts.get()
          if tag == ';':
            break
          (tag_name, tag_rev) = string.split(tag, ':')
          self.sink.define_tag(tag_name, tag_rev)
      elif token == "comment":
        semi, comment = self.ts.mget(2)
        self.sink.set_comment(comment)
        if semi != ';':
          raise common.RCSExpected(semi, ';')

      # Ignore all these other fields - We don't care about them. Also chews
      # up "newphrase".
      elif token in ("locks", "strict", "expand", "access"):
        while 1:
          tag = self.ts.get()
          if tag == ';':
            break
      else:
        pass
        # warn("Unexpected RCS token: $token\n")

    raise RuntimeError, "Unexpected EOF";
