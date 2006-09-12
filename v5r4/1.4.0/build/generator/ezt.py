#!/usr/bin/env python
"""ezt.py -- easy templating

ezt templates are very similar to standard HTML files.  But additionally
they contain directives sprinkled in between.  With these directives
it is possible to generate the dynamic content from the ezt templates.

These directives are enclosed in square brackets.  If you are a 
C-programmer, you might be familar with the #ifdef directives of the
C preprocessor 'cpp'.  ezt provides a similar concept for HTML.  Additionally 
EZT has a 'for' directive, which allows to iterate (repeat) certain 
subsections of the template according to sequence of data items
provided by the application.

The HTML rendering is performed by the method generate() of the Template
class.  Building template instances can either be done using external
EZT files (convention: use the suffix .ezt for such files):

    >>> template = Template("../templates/log.ezt")

or by calling the parse() method of a template instance directly with 
a EZT template string:

    >>> template = Template()
    >>> template.parse('''<html><head>
    ... <title>[title_string]</title></head>
    ... <body><h1>[title_string]</h1>
    ...    [for a_sequence] <p>[a_sequence]</p>
    ...    [end] <hr>
    ...    The [person] is [if-any state]in[else]out[end].
    ... </body>
    ... </html>
    ... ''')

The application should build a dictionary 'data' and pass it together
with the output fileobject to the templates generate method:

    >>> data = {'title_string' : "A Dummy Page",
    ...         'a_sequence' : ['list item 1', 'list item 2', 'another element'],
    ...         'person': "doctor",
    ...         'state' : None }
    >>> import sys
    >>> template.generate(sys.stdout, data)
    <html><head>
    <title>A Dummy Page</title></head>
    <body><h1>A Dummy Page</h1>
     <p>list item 1</p>
     <p>list item 2</p>
     <p>another element</p>
     <hr>
    The doctor is out.
    </body>
    </html>

Template syntax error reporting should be improved.  Currently it is 
very sparse (template line numbers would be nice):

    >>> Template().parse("[if-any where] foo [else] bar [end unexpected args]")
    Traceback (innermost last):
      File "<stdin>", line 1, in ?
      File "ezt.py", line 220, in parse
        self.program = self._parse(text)
      File "ezt.py", line 275, in _parse
        raise ArgCountSyntaxError(str(args[1:]))
    ArgCountSyntaxError: ['unexpected', 'args']
    >>> Template().parse("[if unmatched_end]foo[end]")
    Traceback (innermost last):
      File "<stdin>", line 1, in ?
      File "ezt.py", line 206, in parse
        self.program = self._parse(text)
      File "ezt.py", line 266, in _parse
        raise UnmatchedEndError()
    UnmatchedEndError


Directives
==========

 Several directives allow the use of dotted qualified names refering to objects
 or attributes of objects contained in the data dictionary given to the 
 .generate() method.

 Simple directives
 -----------------

   [QUAL_NAME]

   This directive is simply replaced by the value of identifier from the data 
   dictionary.  QUAL_NAME might be a dotted qualified name refering to some
   instance attribute of objects contained in the dats dictionary.
   Numbers are converted to string though.

   [include "filename"]  or [include QUAL_NAME]

   This directive is replaced by content of the named include file.

 Block directives
 ----------------

   [for QUAL_NAME] ... [end]
   
   The text within the [for ...] directive and the corresponding [end]
   is repeated for each element in the sequence referred to by the qualified
   name in the for directive.  Within the for block this identifiers now 
   refers to the actual item indexed by this loop iteration.

   [if-any QUAL_NAME [QUAL_NAME2 ...]] ... [else] ... [end]

   Test if any QUAL_NAME value is not None or an empty string or list.  
   The [else] clause is optional.  CAUTION: Numeric values are converted to
   string, so if QUAL_NAME refers to a numeric value 0, the then-clause is
   substituted!

   [if-index INDEX_FROM_FOR odd] ... [else] ... [end]
   [if-index INDEX_FROM_FOR even] ... [else] ... [end]
   [if-index INDEX_FROM_FOR first] ... [else] ... [end]
   [if-index INDEX_FROM_FOR last] ... [else] ... [end]
   [if-index INDEX_FROM_FOR NUMBER] ... [else] ... [end]

   These five directives work similar to [if-any], but are only useful 
   within a [for ...]-block (see above).  The odd/even directives are 
   for example useful to choose different background colors for adjacent rows 
   in a table.  Similar the first/last directives might be used to
   remove certain parts (for example "Diff to previous" doesn't make sense,
   if there is no previous).

   [is QUAL_NAME STRING] ... [else] ... [end]
   [is QUAL_NAME QUAL_NAME] ... [else] ... [end]

   The [is ...] directive is similar to the other conditional directives
   above.  But it allows to compare two value references or a value reference
   with some constant string.
 
"""
#
# Copyright (C) 2001-2002 Greg Stein. All Rights Reserved.
#
# Redistribution and use in source and binary forms, with or without 
# modification, are permitted provided that the following conditions are 
# met:
#
# * Redistributions of source code must retain the above copyright 
#   notice, this list of conditions and the following disclaimer. 
#
# * Redistributions in binary form must reproduce the above copyright 
#   notice, this list of conditions and the following disclaimer in the 
#   documentation and/or other materials provided with the distribution. 
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS 
# IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, 
# THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR 
# PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE 
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE 
# POSSIBILITY OF SUCH DAMAGE.
#
#
# This software is maintained by Greg and is available at:
#    http://viewcvs.sourceforge.net/
# it is also used by the following projects:
#    http://edna.sourceforge.net/
#

import string
import re
from types import StringType, IntType, FloatType
import os

#
# This regular expression matches three alternatives:
#   expr: DIRECTIVE | BRACKET | COMMENT
#   DIRECTIVE: '[' ITEM (whitespace ITEM)* ']
#   ITEM: STRING | NAME
#   STRING: '"' (not-slash-or-dquote | '\' anychar)* '"'
#   NAME: (alphanum | '_' | '-' | '.')+
#   BRACKET: '[[]'
#   COMMENT: '[#' not-rbracket* ']'
#
# When used with the split() method, the return value will be composed of
# non-matching text and the two paren groups (DIRECTIVE and BRACKET). Since
# the COMMENT matches are not placed into a group, they are considered a
# "splitting" value and simply dropped.
#
_item = r'(?:"(?:[^\\"]|\\.)*"|[-\w.]+)'
_re_parse = re.compile(r'\[(%s(?: +%s)*)\]|(\[\[\])|\[#[^\]]*\]' % (_item, _item))

_re_args = re.compile(r'"(?:[^\\"]|\\.)*"|[-\w.]+')

# block commands and their argument counts
_block_cmd_specs = { 'if-index':2, 'for':1, 'is':2 }
_block_cmds = _block_cmd_specs.keys()

# two regular expressions for compressing whitespace. the first is used to
# compress any whitespace including a newline into a single newline. the
# second regex is used to compress runs of whitespace into a single space.
_re_newline = re.compile('[ \t\r\f\v]*\n\\s*')
_re_whitespace = re.compile(r'\s\s+')

# this regex is used to substitute arguments into a value. we split the value,
# replace the relevant pieces, and then put it all back together. splitting
# will produce a list of: TEXT ( splitter TEXT )*. splitter will be '%' or
# an integer.
_re_subst = re.compile('%(%|[0-9]+)')

class Template:

  def __init__(self, fname=None, compress_whitespace=1):
    self.compress_whitespace = compress_whitespace
    if fname:
      self.parse_file(fname)

  def parse_file(self, fname):
    "fname -> a string object with pathname of file containg an EZT template."

    self.program = self._parse(_FileReader(fname))

  def parse(self, text_or_reader):
    """Parse the template specified by text_or_reader.

    The argument should be a string containing the template, or it should
    specify a subclass of ezt.Reader which can read templates.
    """
    if not isinstance(text_or_reader, Reader):
      # assume the argument is a plain text string
      text_or_reader = _TextReader(text_or_reader)
    self.program = self._parse(text_or_reader)

  def generate(self, fp, data):
    ctx = _context()
    ctx.data = data
    ctx.for_index = { }
    self._execute(self.program, fp, ctx)

  def _parse(self, reader, for_names=None, file_args=()):
    """text -> string object containing the HTML template.

    This is a private helper function doing the real work for method parse.
    It returns the parsed template as a 'program'.  This program is a sequence
    made out of strings or (function, argument) 2-tuples.

    Note: comment directives [# ...] are automatically dropped by _re_parse.
    """

    # parse the template program into: (TEXT DIRECTIVE BRACKET)* TEXT
    parts = _re_parse.split(reader.text)

    program = [ ]
    stack = [ ]
    if not for_names:
       for_names = [ ]

    for i in range(len(parts)):
      piece = parts[i]
      which = i % 3  # discriminate between: TEXT DIRECTIVE BRACKET
      if which == 0:
        # TEXT. append if non-empty.
        if piece:
          if self.compress_whitespace:
            piece = _re_whitespace.sub(' ', _re_newline.sub('\n', piece))
          program.append(piece)
      elif which == 2:
        # BRACKET directive. append '[' if present.
        if piece:
          program.append('[')
      elif piece:
        # DIRECTIVE is present.
        args = _re_args.findall(piece)
        cmd = args[0]
        if cmd == 'else':
          if len(args) > 1:
            raise ArgCountSyntaxError(str(args[1:]))
          ### check: don't allow for 'for' cmd
          idx = stack[-1][1]
          true_section = program[idx:]
          del program[idx:]
          stack[-1][3] = true_section
        elif cmd == 'end':
          if len(args) > 1:
            raise ArgCountSyntaxError(str(args[1:]))
          # note: true-section may be None
          try:
            cmd, idx, args, true_section = stack.pop()
          except IndexError:
            raise UnmatchedEndError()
          else_section = program[idx:]
          func = getattr(self, '_cmd_' + re.sub('-', '_', cmd))
          program[idx:] = [ (func, (args, true_section, else_section)) ]
          if cmd == 'for':
            for_names.pop()
        elif cmd in _block_cmds:
          if len(args) > _block_cmd_specs[cmd] + 1:
            raise ArgCountSyntaxError(str(args[1:]))
          ### this assumes arg1 is always a ref
          args[1] = _prepare_ref(args[1], for_names, file_args)

          # handle arg2 for the 'is' command
          if cmd == 'is':
            args[2] = _prepare_ref(args[2], for_names, file_args)
          elif cmd == 'for':
            for_names.append(args[1][0])

          # remember the cmd, current pos, args, and a section placeholder
          stack.append([cmd, len(program), args[1:], None])
        elif cmd == 'include':
          if args[1][0] == '"':
            include_filename = args[1][1:-1]
            f_args = [ ]
            for arg in args[2:]:
              f_args.append(_prepare_ref(arg, for_names, file_args))
            program.extend(self._parse(reader.read_other(include_filename),
                                       for_names,
                                       f_args))
          else:
            if len(args) != 2:
              raise ArgCountSyntaxError(str(args))
            program.append((self._cmd_include,
                            (_prepare_ref(args[1], for_names, file_args),
                             reader)))
        elif cmd == 'if-any':
          f_args = [ ]
          for arg in args[1:]:
            f_args.append(_prepare_ref(arg, for_names, file_args))
          stack.append(['if-any', len(program), f_args, None])
        else:
          # implied PRINT command
          if len(args) > 1:
            f_args = [ ]
            for arg in args:
              f_args.append(_prepare_ref(arg, for_names, file_args))
            program.append((self._cmd_format, (f_args[0], f_args[1:])))
          else:
            program.append((self._cmd_print,
                            _prepare_ref(args[0], for_names, file_args)))

    if stack:
      ### would be nice to say which blocks...
      raise UnclosedBlocksError()
    return program

  def _execute(self, program, fp, ctx):
    """This private helper function takes a 'program' sequence as created
    by the method '_parse' and executes it step by step.  strings are written
    to the file object 'fp' and functions are called.
    """
    for step in program:
      if isinstance(step, StringType):
        fp.write(step)
      else:
        step[0](step[1], fp, ctx)

  def _cmd_print(self, valref, fp, ctx):
    value = _get_value(valref, ctx)

    # if the value has a 'read' attribute, then it is a stream: copy it
    if hasattr(value, 'read'):
      while 1:
        chunk = value.read(16384)
        if not chunk:
          break
        fp.write(chunk)
    else:
      fp.write(value)

  def _cmd_format(self, (valref, args), fp, ctx):
    fmt = _get_value(valref, ctx)
    parts = _re_subst.split(fmt)
    for i in range(len(parts)):
      piece = parts[i]
      if i%2 == 1 and piece != '%':
        idx = int(piece)
        if idx < len(args):
          piece = _get_value(args[idx], ctx)
        else:
          piece = '<undef>'
      fp.write(piece)

  def _cmd_include(self, (valref, reader), fp, ctx):
    fname = _get_value(valref, ctx)
    ### note: we don't have the set of for_names to pass into this parse.
    ### I don't think there is anything to do but document it.
    self._execute(self._parse(reader.read_other(fname)), fp, ctx)

  def _cmd_if_any(self, args, fp, ctx):
    "If any value is a non-empty string or non-empty list, then T else F."
    (valrefs, t_section, f_section) = args
    value = 0
    for valref in valrefs:
      if _get_value(valref, ctx):
        value = 1
        break
    self._do_if(value, t_section, f_section, fp, ctx)

  def _cmd_if_index(self, args, fp, ctx):
    ((valref, value), t_section, f_section) = args
    list, idx = ctx.for_index[valref[0]]
    if value == 'even':
      value = idx % 2 == 0
    elif value == 'odd':
      value = idx % 2 == 1
    elif value == 'first':
      value = idx == 0
    elif value == 'last':
      value = idx == len(list)-1
    else:
      value = idx == int(value)
    self._do_if(value, t_section, f_section, fp, ctx)

  def _cmd_is(self, args, fp, ctx):
    ((left_ref, right_ref), t_section, f_section) = args
    value = _get_value(right_ref, ctx)
    value = string.lower(_get_value(left_ref, ctx)) == string.lower(value)
    self._do_if(value, t_section, f_section, fp, ctx)

  def _do_if(self, value, t_section, f_section, fp, ctx):
    if t_section is None:
      t_section = f_section
      f_section = None
    if value:
      section = t_section
    else:
      section = f_section
    if section is not None:
      self._execute(section, fp, ctx)

  def _cmd_for(self, args, fp, ctx):
    ((valref,), unused, section) = args
    list = _get_value(valref, ctx)
    if isinstance(list, StringType):
      raise NeedSequenceError()
    refname = valref[0]
    ctx.for_index[refname] = idx = [ list, 0 ]
    for item in list:
      self._execute(section, fp, ctx)
      idx[1] = idx[1] + 1
    del ctx.for_index[refname]

def boolean(value):
  "Return a value suitable for [if-any bool_var] usage in a template."
  if value:
    return 'yes'
  return None


def _prepare_ref(refname, for_names, file_args):
  """refname -> a string containing a dotted identifier. example:"foo.bar.bang"
  for_names -> a list of active for sequences.

  Returns a `value reference', a 3-Tupel made out of (refname, start, rest), 
  for fast access later.
  """
  # is the reference a string constant?
  if refname[0] == '"':
    return None, refname[1:-1], None

  # if this is an include-argument, then just return the prepared ref
  if refname[:3] == 'arg':
    try:
      idx = int(refname[3:])
    except ValueError:
      pass
    else:
      if idx < len(file_args):
        return file_args[idx]

  parts = string.split(refname, '.')
  start = parts[0]
  rest = parts[1:]
  while rest and (start in for_names):
    # check if the next part is also a "for name"
    name = start + '.' + rest[0]
    if name in for_names:
      start = name
      del rest[0]
    else:
      break
  return refname, start, rest

def _get_value((refname, start, rest), ctx):
  """(refname, start, rest) -> a prepared `value reference' (see above).
  ctx -> an execution context instance.

  Does a name space lookup within the template name space.  Active 
  for blocks take precedence over data dictionary members with the 
  same name.
  """
  if rest is None:
    # it was a string constant
    return start
  if ctx.for_index.has_key(start):
    list, idx = ctx.for_index[start]
    ob = list[idx]
  elif ctx.data.has_key(start):
    ob = ctx.data[start]
  else:
    raise UnknownReference(refname)

  # walk the rest of the dotted reference
  for attr in rest:
    try:
      ob = getattr(ob, attr)
    except AttributeError:
      raise UnknownReference(refname)

  # make sure we return a string instead of some various Python types
  if isinstance(ob, IntType) or isinstance(ob, FloatType):
    return str(ob)
  if ob is None:
    return ''

  # string or a sequence
  return ob


class _context:
  """A container for the execution context"""


class Reader:
  "Abstract class which allows EZT to detect Reader objects."

class _FileReader(Reader):
  """Reads templates from the filesystem."""
  def __init__(self, fname):
    self.text = open(fname, 'rb').read()
    self._dir = os.path.dirname(fname)
  def read_other(self, relative):
    return _FileReader(os.path.join(self._dir, relative))

class _TextReader(Reader):
  """'Reads' a template from provided text."""
  def __init__(self, text):
    self.text = text
  def read_other(self, relative):
    raise BaseUnavailableError()


class EZTException(Exception):
  """Parent class of all EZT exceptions."""

class ArgCountSyntaxError(EZTException):
  """A bracket directive got the wrong number of arguments."""

class UnknownReference(EZTException):
  """The template references an object not contained in the data dictionary."""

class NeedSequenceError(EZTException):
  """The object dereferenced by the template is no sequence (tuple or list)."""

class UnclosedBlocksError(EZTException):
  """This error may be simply a missing [end]."""

class UnmatchedEndError(EZTException):
  """This error may be caused by a misspelled if directive."""

class BaseUnavailableError(EZTException):
  """Base location is unavailable, which disables includes."""


# --- standard test environment ---
def test_parse():
  assert _re_parse.split('[a]') == ['', '[a]', None, '']
  assert _re_parse.split('[a] [b]') == \
         ['', '[a]', None, ' ', '[b]', None, '']
  assert _re_parse.split('[a c] [b]') == \
         ['', '[a c]', None, ' ', '[b]', None, '']
  assert _re_parse.split('x [a] y [b] z') == \
         ['x ', '[a]', None, ' y ', '[b]', None, ' z']
  assert _re_parse.split('[a "b" c "d"]') == \
         ['', '[a "b" c "d"]', None, '']
  assert _re_parse.split(r'["a \"b[foo]" c.d f]') == \
         ['', '["a \\"b[foo]" c.d f]', None, '']

def _test(argv):
  import doctest, ezt           
  verbose = "-v" in argv
  return doctest.testmod(ezt, verbose=verbose)

if __name__ == "__main__":
  # invoke unit test for this module:
  import sys
  sys.exit(_test(sys.argv)[0])
