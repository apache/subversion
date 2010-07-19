#!/usr/bin/env python
#
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#
#
"""ezt.py -- easy templating

ezt templates are simply text files in whatever format you so desire
(such as XML, HTML, etc.) which contain directives sprinkled
throughout.  With these directives it is possible to generate the
dynamic content from the ezt templates.

These directives are enclosed in square brackets.  If you are a
C-programmer, you might be familar with the #ifdef directives of the C
preprocessor 'cpp'.  ezt provides a similar concept.  Additionally EZT
has a 'for' directive, which allows it to iterate (repeat) certain
subsections of the template according to sequence of data items
provided by the application.

The final rendering is performed by the method generate() of the Template
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

 Qualified names
 ---------------

   Qualified names have two basic forms: a variable reference, or a string
   constant. References are a name from the data dictionary with optional
   dotted attributes (where each intermediary is an object with attributes,
   of course).

   Examples:

     [varname]

     [ob.attr]

     ["string"]

 Simple directives
 -----------------

   [QUAL_NAME]

   This directive is simply replaced by the value of the qualified name.
   Numbers are converted to a string, and None becomes an empty string.

   [QUAL_NAME QUAL_NAME ...]

   The first value defines a substitution format, specifying constant
   text and indices of the additional arguments. The arguments are then
   substituted and the resulting is inserted into the output stream.

   Example:
     ["abc %0 def %1 ghi %0" foo bar.baz]

   Note that the first value can be any type of qualified name -- a string
   constant or a variable reference. Use %% to substitute a percent sign.
   Argument indices are 0-based.

   [include "filename"]  or [include QUAL_NAME]

   This directive is replaced by content of the named include file. Note
   that a string constant is more efficient -- the target file is compiled
   inline. In the variable form, the target file is compiled and executed
   at runtime.

   [insertfile "filename"] or [insertfile QUAL_NAME]

   This directive is replace by content from the named file, but as a
   literal string: directives in the target file are not expanded.  As
   in the case of the "include" directive, using a string constant for
   the filename is more efficient than the variable form.

 Block directives
 ----------------

   [for QUAL_NAME] ... [end]

   The text within the [for ...] directive and the corresponding [end]
   is repeated for each element in the sequence referred to by the
   qualified name in the for directive.  Within the for block this
   identifiers now refers to the actual item indexed by this loop
   iteration.

   [if-any QUAL_NAME [QUAL_NAME2 ...]] ... [else] ... [end]

   Test if any QUAL_NAME value is not None or an empty string or list.
   The [else] clause is optional.  CAUTION: Numeric values are
   converted to string, so if QUAL_NAME refers to a numeric value 0,
   the then-clause is substituted!

   [if-index INDEX_FROM_FOR odd] ... [else] ... [end]
   [if-index INDEX_FROM_FOR even] ... [else] ... [end]
   [if-index INDEX_FROM_FOR first] ... [else] ... [end]
   [if-index INDEX_FROM_FOR last] ... [else] ... [end]
   [if-index INDEX_FROM_FOR NUMBER] ... [else] ... [end]

   These five directives work similar to [if-any], but are only useful
   within a [for ...]-block (see above).  The odd/even directives are
   for example useful to choose different background colors for
   adjacent rows in a table.  Similar the first/last directives might
   be used to remove certain parts (for example "Diff to previous"
   doesn't make sense, if there is no previous).

   [is QUAL_NAME STRING] ... [else] ... [end]
   [is QUAL_NAME QUAL_NAME] ... [else] ... [end]

   The [is ...] directive is similar to the other conditional
   directives above.  But it allows to compare two value references or
   a value reference with some constant string.

   [define VARIABLE] ... [end]

   The [define ...] directive allows you to create and modify template
   variables from within the template itself.  Essentially, any data
   between inside the [define ...] and its matching [end] will be
   expanded using the other template parsing and output generation
   rules, and then stored as a string value assigned to the variable
   VARIABLE.  The new (or changed) variable is then available for use
   with other mechanisms such as [is ...] or [if-any ...], as long as
   they appear later in the template.

   [format "html|xml|js|url|raw"] ... [end]

   The [format ...] directive creates a block in which any substitutions
   are processed as though the template has been instantiated with the
   the corresponding 'base_format' argument. Comma-separated format
   specifiers perform nested encodings. In this case the encodings are
   applied left-to-right.  For example the directive: [format "html,js"]
   will HTML and then Javascript encode any inserted template variables.
"""
#
# Copyright (C) 2001-2009 Greg Stein. All Rights Reserved.
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
#    http://code.google.com/p/ezt/
#

import os, re, sys

if sys.version_info[0] >= 3:
  # Python >=3.0
  long = int
  unicode = str
  from io import StringIO
  from urllib.parse import quote_plus as urllib_parse_quote_plus
else:
  # Python <3.0
  from urllib import quote_plus as urllib_parse_quote_plus
  try:
    from cStringIO import StringIO
  except ImportError:
    from StringIO import StringIO

#
# Formatting types
#
FORMAT_RAW = 'raw'
FORMAT_HTML = 'html'
FORMAT_XML = 'xml'
FORMAT_JS = 'js'
FORMAT_URL = 'url'

#
# This regular expression matches three alternatives:
#   expr: NEWLINE | DIRECTIVE | BRACKET | COMMENT
#   DIRECTIVE: '[' ITEM (whitespace ITEM)* ']
#   ITEM: STRING | NAME
#   STRING: '"' (not-slash-or-dquote | '\' anychar)* '"'
#   NAME: (alphanum | '_' | '-' | '.')+
#   BRACKET: '[[]'
#   COMMENT: '[#' not-rbracket* ']'
#
# When used with the split() method, the return value will be composed of
# non-matching text and the three paren groups (NEWLINE, DIRECTIVE and
# BRACKET). Since the COMMENT matches are not placed into a group, they are
# considered a "splitting" value and simply dropped.
#
_item = r'(?:"(?:[^\\"]|\\.)*"|[-\w.]+)'
_re_parse = re.compile(r'(\r?\n)|\[(%s(?: +%s)*)\]|(\[\[\])|\[#[^\]]*\]' %
                       (_item, _item))

_re_args = re.compile(r'"(?:[^\\"]|\\.)*"|[-\w.]+')

# block commands and their argument counts
_block_cmd_specs = { 'if-index':2, 'for':1, 'is':2, 'define':1, 'format':1 }
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

  def __init__(self, fname=None, compress_whitespace=1,
               base_format=FORMAT_RAW):
    self.compress_whitespace = compress_whitespace
    if fname:
      self.parse_file(fname, base_format)

  def parse_file(self, fname, base_format=FORMAT_RAW):
    "fname -> a string object with pathname of file containg an EZT template."

    self.parse(_FileReader(fname), base_format)

  def parse(self, text_or_reader, base_format=FORMAT_RAW):
    """Parse the template specified by text_or_reader.

    The argument should be a string containing the template, or it should
    specify a subclass of ezt.Reader which can read templates. The base
    format for printing values is given by base_format.
    """
    if not isinstance(text_or_reader, Reader):
      # assume the argument is a plain text string
      text_or_reader = _TextReader(text_or_reader)

    self.program = self._parse(text_or_reader,
                               base_printer=_parse_format(base_format))

  def generate(self, fp, data):
    if hasattr(data, '__getitem__') or hasattr(getattr(data, 'keys', None), '__call__'):
      # a dictionary-like object was passed. convert it to an
      # attribute-based object.
      class _data_ob:
        def __init__(self, d):
          vars(self).update(d)
      data = _data_ob(data)

    ctx = _context()
    ctx.data = data
    ctx.for_index = { }
    ctx.defines = { }
    self._execute(self.program, fp, ctx)

  def _parse(self, reader, for_names=None, file_args=(), base_printer=None):
    """text -> string object containing the template.

    This is a private helper function doing the real work for method parse.
    It returns the parsed template as a 'program'.  This program is a sequence
    made out of strings or (function, argument) 2-tuples.

    Note: comment directives [# ...] are automatically dropped by _re_parse.
    """

    filename = reader.filename()
    # parse the template program into: (TEXT NEWLINE DIRECTIVE BRACKET)* TEXT
    parts = _re_parse.split(reader.text)

    program = [ ]
    stack = [ ]
    if not for_names:
      for_names = [ ]

    if base_printer is None:
      base_printer = ()
    printers = [ base_printer ]

    one_newline_copied = False
    line_number = 1
    for i in range(len(parts)):
      piece = parts[i]
      which = i % 4  # discriminate between: TEXT NEWLINE DIRECTIVE BRACKET
      if which == 0:
        # TEXT. append if non-empty.
        if piece:
          if self.compress_whitespace:
            piece = _re_whitespace.sub(' ', piece)
          program.append(piece)
          one_newline_copied = False
      elif which == 1:
        # NEWLINE. append unless compress_whitespace requested
        if piece:
          line_number += 1
          if self.compress_whitespace:
            if not one_newline_copied:
              program.append('\n')
              one_newline_copied = True
          else:
            program.append(piece)
      elif which == 3:
        # BRACKET directive. append '[' if present.
        if piece:
          program.append('[')
          one_newline_copied = False
      elif piece:
        # DIRECTIVE is present.
        one_newline_copied = False
        args = _re_args.findall(piece)
        cmd = args[0]
        if cmd == 'else':
          if len(args) > 1:
            raise ArgCountSyntaxError(str(args[1:]), filename, line_number)
          ### check: don't allow for 'for' cmd
          idx = stack[-1][1]
          true_section = program[idx:]
          del program[idx:]
          stack[-1][3] = true_section
        elif cmd == 'end':
          if len(args) > 1:
            raise ArgCountSyntaxError(str(args[1:]), filename, line_number)
          # note: true-section may be None
          try:
            cmd, idx, args, true_section, start_line_number = stack.pop()
          except IndexError:
            raise UnmatchedEndError(None, filename, line_number)
          else_section = program[idx:]
          if cmd == 'format':
            printers.pop()
          else:
            func = getattr(self, '_cmd_' + re.sub('-', '_', cmd))
            program[idx:] = [ (func, (args, true_section, else_section),
                               filename, line_number) ]
            if cmd == 'for':
              for_names.pop()
        elif cmd in _block_cmds:
          if len(args) > _block_cmd_specs[cmd] + 1:
            raise ArgCountSyntaxError(str(args[1:]), filename, line_number)
          ### this assumes arg1 is always a ref unless cmd is 'define'
          if cmd != 'define':
            args[1] = _prepare_ref(args[1], for_names, file_args)

          # handle arg2 for the 'is' command
          if cmd == 'is':
            args[2] = _prepare_ref(args[2], for_names, file_args)
          elif cmd == 'for':
            for_names.append(args[1][0])  # append the refname
          elif cmd == 'format':
            if args[1][0]:
              raise BadFormatConstantError(str(args[1:]), filename, line_number)
            printers.append(_parse_format(args[1][1]))

          # remember the cmd, current pos, args, and a section placeholder
          stack.append([cmd, len(program), args[1:], None, line_number])
        elif cmd == 'include' or cmd == 'insertfile':
          is_insertfile = (cmd == 'insertfile')
          # extra arguments are meaningless when using insertfile
          if is_insertfile and len(args) != 2:
            raise ArgCountSyntaxError(str(args), filename, line_number)
          if args[1][0] == '"':
            include_filename = args[1][1:-1]
            if is_insertfile:
              program.append(reader.read_other(include_filename).text)
            else:
              f_args = [ ]
              for arg in args[2:]:
                f_args.append(_prepare_ref(arg, for_names, file_args))
              program.extend(self._parse(reader.read_other(include_filename),
                                         for_names, f_args, printers[-1]))
          else:
            if len(args) != 2:
              raise ArgCountSyntaxError(str(args), filename, line_number)
            if is_insertfile:
              cmd = self._cmd_insertfile
            else:
              cmd = self._cmd_include
            program.append((cmd,
                            (_prepare_ref(args[1], for_names, file_args),
                             reader, printers[-1]), filename, line_number))
        elif cmd == 'if-any':
          f_args = [ ]
          for arg in args[1:]:
            f_args.append(_prepare_ref(arg, for_names, file_args))
          stack.append(['if-any', len(program), f_args, None, line_number])
        else:
          # implied PRINT command
          if len(args) > 1:
            f_args = [ ]
            for arg in args:
              f_args.append(_prepare_ref(arg, for_names, file_args))
            program.append((self._cmd_subst,
                            (printers[-1], f_args[0], f_args[1:]),
                            filename, line_number))
          else:
            valref = _prepare_ref(args[0], for_names, file_args)
            program.append((self._cmd_print, (printers[-1], valref),
                            filename, line_number))

    if stack:
      raise UnclosedBlocksError('Block opened at line %s' % stack[-1][4],
                                filename=filename)
    return program

  def _execute(self, program, fp, ctx):
    """This private helper function takes a 'program' sequence as created
    by the method '_parse' and executes it step by step.  strings are written
    to the file object 'fp' and functions are called.
    """
    for step in program:
      if isinstance(step, str):
        fp.write(step)
      else:
        method, method_args, filename, line_number = step
        method(method_args, fp, ctx, filename, line_number)

  def _cmd_print(self, transforms_valref, fp, ctx, filename, line_number):
    (transforms, valref) = transforms_valref
    value = _get_value(valref, ctx, filename, line_number)
    # if the value has a 'read' attribute, then it is a stream: copy it
    if hasattr(value, 'read'):
      while 1:
        chunk = value.read(16384)
        if not chunk:
          break
        for t in transforms:
          chunk = t(chunk)
        fp.write(chunk)
    else:
      for t in transforms:
        value = t(value)
      fp.write(value)

  def _cmd_subst(self, transforms_valref_args, fp, ctx, filename,
                 line_number):
    (transforms, valref, args) = transforms_valref_args
    fmt = _get_value(valref, ctx, filename, line_number)
    parts = _re_subst.split(fmt)
    for i in range(len(parts)):
      piece = parts[i]
      if i%2 == 1 and piece != '%':
        idx = int(piece)
        if idx < len(args):
          piece = _get_value(args[idx], ctx, filename, line_number)
        else:
          piece = '<undef>'
      for t in transforms:
        piece = t(piece)
      fp.write(piece)

  def _cmd_include(self, valref_reader_printer, fp, ctx, filename,
                   line_number):
    (valref, reader, printer) = valref_reader_printer
    fname = _get_value(valref, ctx, filename, line_number)
    ### note: we don't have the set of for_names to pass into this parse.
    ### I don't think there is anything to do but document it
    self._execute(self._parse(reader.read_other(fname), base_printer=printer),
                  fp, ctx)

  def _cmd_insertfile(self, valref_reader_printer, fp, ctx, filename,
                      line_number):
    (valref, reader, printer) = valref_reader_printer
    fname = _get_value(valref, ctx, filename, line_number)
    fp.write(reader.read_other(fname).text)

  def _cmd_if_any(self, args, fp, ctx, filename, line_number):
    "If any value is a non-empty string or non-empty list, then T else F."
    (valrefs, t_section, f_section) = args
    value = 0
    for valref in valrefs:
      if _get_value(valref, ctx, filename, line_number):
        value = 1
        break
    self._do_if(value, t_section, f_section, fp, ctx)

  def _cmd_if_index(self, args, fp, ctx, filename, line_number):
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

  def _cmd_is(self, args, fp, ctx, filename, line_number):
    ((left_ref, right_ref), t_section, f_section) = args
    right_value = _get_value(right_ref, ctx, filename, line_number)
    left_value = _get_value(left_ref, ctx, filename, line_number)
    value = left_value.lower() == right_value.lower()
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

  def _cmd_for(self, args, fp, ctx, filename, line_number):
    ((valref,), unused, section) = args
    list = _get_value(valref, ctx, filename, line_number)
    refname = valref[0]
    if isinstance(list, str):
      raise NeedSequenceError(refname, filename, line_number)
    ctx.for_index[refname] = idx = [ list, 0 ]
    for item in list:
      self._execute(section, fp, ctx)
      idx[1] = idx[1] + 1
    del ctx.for_index[refname]

  def _cmd_define(self, args, fp, ctx, filename, line_number):
    ((name,), unused, section) = args
    valfp = StringIO()
    if section is not None:
      self._execute(section, valfp, ctx)
    ctx.defines[name] = valfp.getvalue()

def boolean(value):
  "Return a value suitable for [if-any bool_var] usage in a template."
  if value:
    return 'yes'
  return None


def _prepare_ref(refname, for_names, file_args):
  """refname -> a string containing a dotted identifier. example:"foo.bar.bang"
  for_names -> a list of active for sequences.

  Returns a `value reference', a 3-tuple made out of (refname, start, rest),
  for fast access later.
  """
  # is the reference a string constant?
  if refname[0] == '"':
    return None, refname[1:-1], None

  parts = refname.split('.')
  start = parts[0]
  rest = parts[1:]

  # if this is an include-argument, then just return the prepared ref
  if start[:3] == 'arg':
    try:
      idx = int(start[3:])
    except ValueError:
      pass
    else:
      if idx < len(file_args):
        orig_refname, start, more_rest = file_args[idx]
        if more_rest is None:
          # the include-argument was a string constant
          return None, start, None

        # prepend the argument's "rest" for our further processing
        rest[:0] = more_rest

        # rewrite the refname to ensure that any potential 'for' processing
        # has the correct name
        ### this can make it hard for debugging include files since we lose
        ### the 'argNNN' names
        if not rest:
          return start, start, [ ]
        refname = start + '.' + '.'.join(rest)

  if for_names:
    # From last to first part, check if this reference is part of a for loop
    for i in range(len(parts), 0, -1):
      name = '.'.join(parts[:i])
      if name in for_names:
        return refname, name, parts[i:]

  return refname, start, rest

def _get_value(refname_start_rest, ctx, filename, line_number):
  """refname_start_rest -> a prepared `value reference' (see above).
  ctx -> an execution context instance.

  Does a name space lookup within the template name space.  Active
  for blocks take precedence over data dictionary members with the
  same name.
  """
  (refname, start, rest) = refname_start_rest
  if rest is None:
    # it was a string constant
    return start

  # get the starting object
  if start in ctx.for_index:
    list, idx = ctx.for_index[start]
    ob = list[idx]
  elif start in ctx.defines:
    ob = ctx.defines[start]
  elif hasattr(ctx.data, start):
    ob = getattr(ctx.data, start)
  else:
    raise UnknownReference(refname, filename, line_number)

  # walk the rest of the dotted reference
  for attr in rest:
    try:
      ob = getattr(ob, attr)
    except AttributeError:
      raise UnknownReference(refname, filename, line_number)

  # make sure we return a string instead of some various Python types
  if isinstance(ob, (int, long, float)):
    return str(ob)
  if ob is None:
    return ''

  # string or a sequence
  return ob

def _replace(s, replace_map):
  for orig, repl in replace_map:
    s = s.replace(orig, repl)
  return s

REPLACE_JS_MAP = (
  ('\\', r'\\'), ('\t', r'\t'), ('\n', r'\n'), ('\r', r'\r'),
  ('"', r'\x22'), ('\'', r'\x27'), ('&', r'\x26'),
  ('<', r'\x3c'), ('>', r'\x3e'), ('=', r'\x3d'),
)

# Various unicode whitespace
if sys.version_info[0] >= 3:
  # Python >=3.0
  REPLACE_JS_UNICODE_MAP = (
    ('\u0085', r'\u0085'), ('\u2028', r'\u2028'), ('\u2029', r'\u2029')
  )
else:
  # Python <3.0
  REPLACE_JS_UNICODE_MAP = eval("((u'\u0085', r'\u0085'), (u'\u2028', r'\u2028'), (u'\u2029', r'\u2029'))")

# Why not cgi.escape? It doesn't do single quotes which are occasionally
# used to contain HTML attributes and event handler definitions (unfortunately)
REPLACE_HTML_MAP = (
  ('&', '&amp;'), ('<', '&lt;'), ('>', '&gt;'),
  ('"', '&quot;'), ('\'', '&#39;'),
)

def _js_escape(s):
  s = _replace(s, REPLACE_JS_MAP)
  ### perhaps attempt to coerce the string to unicode and then replace?
  if isinstance(s, unicode):
    s = _replace(s, REPLACE_JS_UNICODE_MAP)
  return s

def _html_escape(s):
  return _replace(s, REPLACE_HTML_MAP)

def _url_escape(s):
  ### quote_plus barfs on non-ASCII characters. According to
  ### http://www.w3.org/International/O-URL-code.html URIs should be
  ### UTF-8 encoded first.
  if isinstance(s, unicode):
    s = s.encode('utf8')
  return urllib_parse_quote_plus(s)

FORMATTERS = {
  FORMAT_RAW: None,
  FORMAT_HTML: _html_escape,
  FORMAT_XML: _html_escape,   ### use the same quoting as HTML for now
  FORMAT_JS: _js_escape,
  FORMAT_URL: _url_escape,
}

def _parse_format(format_string=FORMAT_RAW):
  format_funcs = []
  try:
    for fspec in format_string.split(','):
      format_func = FORMATTERS[fspec]
      if format_func is not None:
        format_funcs.append(format_func)
  except KeyError:
    raise UnknownFormatConstantError(format_string)
  return format_funcs

class _context:
  """A container for the execution context"""


class Reader:
  """Abstract class which allows EZT to detect Reader objects."""
  def filename(self):
    return '(%s does not provide filename() method)' % repr(self)

class _FileReader(Reader):
  """Reads templates from the filesystem."""
  def __init__(self, fname):
    self.text = open(fname, 'rb').read()
    if sys.version_info[0] >= 3:
      # Python >=3.0
      self.text = self.text.decode()
    self._dir = os.path.dirname(fname)
    self.fname = fname
  def read_other(self, relative):
    return _FileReader(os.path.join(self._dir, relative))
  def filename(self):
    return self.fname

class _TextReader(Reader):
  """'Reads' a template from provided text."""
  def __init__(self, text):
    self.text = text
  def read_other(self, relative):
    raise BaseUnavailableError()
  def filename(self):
    return '(text)'


class EZTException(Exception):
  """Parent class of all EZT exceptions."""
  def __init__(self, message=None, filename=None, line_number=None):
    self.message = message
    self.filename = filename
    self.line_number = line_number
  def __str__(self):
    ret = []
    if self.message is not None:
      ret.append(self.message)
    if self.filename is not None:
      ret.append('in file ' + str(self.filename))
    if self.line_number is not None:
      ret.append('at line ' + str(self.line_number))
    return ' '.join(ret)

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

class BadFormatConstantError(EZTException):
  """Format specifiers must be string constants."""

class UnknownFormatConstantError(EZTException):
  """The format specifier is an unknown value."""


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
