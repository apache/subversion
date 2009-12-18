#!/usr/bin/env python

"""\
This tool regenerates and replaces the ToC in an HTML file from the actual
structure of <div>s and <h[2345]>s present in the body of the document.
The section to be overwritten is identified as the XML subtree
rooted at <ol id="toc">.

Usage: ./toctool.py filename...
"""

import sys
import os
import xml.parsers.expat


class Index:
  def __init__(self):
    self.title = None
    self.tree = []
    self._ptr_stack = [self.tree]

  def addLevel(self, id, title):
    newlevel = [(id, title)]
    self._ptr_stack[-1].append(newlevel)
    self._ptr_stack.append(newlevel)

  def upLevel(self):
    self._ptr_stack.pop(-1)

  def prettyString(self):
    out = []
    def step(ilevel, node):
      if type(node) == list:
        for subnode in node:
          step(ilevel+1, subnode)
      else:
        out.append("%s%s" % ("  "*ilevel, node))
    step(-2, self.tree)
    return "\n".join(out)

  def renderXML(self):
    out = []
    def step(ilevel, node):
      if len(node) == 1:
        out.append('%s<li><a href="#%s">%s</a></li>'
            % ('  '*ilevel, node[0][0], node[0][1]))
      else:
        out.append('%s<li><a href="#%s">%s</a>'
            % ('  '*ilevel, node[0][0], node[0][1]))
        out.append('%s<ol>' % ('  '*ilevel))
        for subnode in node[1:]:
          step(ilevel+1, subnode)
        out.append('%s</ol>' % ('  '*ilevel))
        out.append('%s</li> <!-- %s -->' % ('  '*ilevel, node[0][0]))
    out.append('<ol id="toc">')
    for node in self.tree:
      step(1, node)
    out.append('</ol>')
    return "\n".join(out)


class ExpatParseJob:
  def parse(self, file):
    p = xml.parsers.expat.ParserCreate()
    p.ordered_attributes = self._ordered_attributes
    p.returns_unicode = False
    p.specified_attributes = True
    for name in dir(self):
      if name.endswith('Handler'):
        setattr(p, name, getattr(self, name))
    p.ParseFile(file)


class IndexBuildParse(ExpatParseJob):
  keys = {'h2':None, 'h3':None, 'h4':None, 'h5':None}

  def __init__(self):
    self.index = Index()
    self.keyptr = 0
    self.collecting_text = False
    self.text = ''
    self.waiting_for_elt = None
    self.saved_id = None
    self.elt_stack = []
    self._ordered_attributes = False

  def StartElementHandler(self, name, attrs):
    if name == 'div':
      cl = attrs.get('class')
      if cl in self.keys:
        self.waiting_for_elt = cl
        self.saved_id = attrs.get('id')
        self.elt_stack.append((name, True))
        return
    elif name == 'title':
      self.collecting_text = name
      self.text = ''
    elif name == self.waiting_for_elt:
      self.waiting_for_elt = None
      self.collecting_text = name
      self.text = ''
    self.elt_stack.append((name, False))

  def EndElementHandler(self, name):
    if self.collecting_text:
      if name == self.collecting_text:
        if name == 'title':
          self.index.title = self.text
        else:
          self.index.addLevel(self.saved_id, self.text)
          self.saved_id = None
        self.collecting_text = False
      else:
        raise RuntimeError('foo')
    eltinfo = self.elt_stack.pop(-1)
    assert eltinfo[0] == name
    if eltinfo[1]:
      self.index.upLevel()

  def DefaultHandler(self, data) :
    if self.collecting_text:
      self.text += data


def attrlist_to_dict(l):
  d = {}
  for i in range(0, len(l), 2):
    d[l[i]] = l[i+1]
  return d


def escape_entities(s):
  return s.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')


class IndexInsertParse(ExpatParseJob):
  def __init__(self, index, outfp):
    self._ordered_attributes = True
    self.index = index
    self.outfp = outfp
    self.elt_stack = []
    self.skipping_toc = False

    self._line_in_progress = []
    self._element_open = None
    self.linepos = 0
    self.indentpos = 0

    self.do_not_minimize = {'script':None}
    self.do_not_indent = {'div':None, 'a':None, 'strong':None, 'em':None}
    self.do_not_wrap = {'div':None, 'strong':None, 'em':None, 'li':None}

    if self.index.title == 'Subversion Design':
      self.do_not_wrap['a'] = None

  def put_token(self, token, tag_name):
    self._line_in_progress.append((token, tag_name))

  def done_line(self):
    linepos = 0
    last_was_tag = False
    outq = []
    for token, tag_name in self._line_in_progress:
      is_tag = tag_name is not None and tag_name not in self.do_not_wrap
      no_indent_if_wrap = tag_name in self.do_not_indent
      linepos += len(token)
      if linepos > 79 and is_tag and last_was_tag:
        token = token.lstrip(' ')
        if no_indent_if_wrap:
          linepos = len(token)
          outq.append('\n')
        else:
          linepos = len(token) + 2
          outq.append('\n  ')
      outq.append(token)
      last_was_tag = is_tag
    outq.append('\n')
    for i in outq:
      self.outfp.write(i)
    del self._line_in_progress[:]

  def _finish_pending(self, minimized_form):
    if self._element_open is not None:
      name = self._element_open
      self._element_open = None
      if minimized_form:
        self.put_token(' />', name)
        return True
      else:
        self.put_token('>', name)
    return False

  def StartElementHandler(self, name, attrs):
    self._finish_pending(False)
    if name == 'ol' and attrlist_to_dict(attrs).get('id') == 'toc':
      self.outfp.write(self.index.renderXML())
      self.skipping_toc = True
      self.elt_stack.append((name, True))
      return
    if not self.skipping_toc:
      self.put_token("<%s" % name, name)
      while attrs:
        aname = attrs.pop(0)
        aval = escape_entities(attrs.pop(0))
        self.put_token(' %s="%s"' % (aname, aval), name)
      self._element_open = name
    self.elt_stack.append((name, False))

  def EndElementHandler(self, name):
    if not self.skipping_toc:
      if not self._finish_pending(name not in self.do_not_minimize):
        self.put_token("</%s>" % name, name)
    eltinfo = self.elt_stack.pop(-1)
    assert eltinfo[0] == name
    if eltinfo[1]:
      self.skipping_toc = False

  def DefaultHandler(self, data):
    if self.skipping_toc:
      return
    self._finish_pending(False)
    # This makes an unsafe assumption that expat will pass '\n' as individual
    # characters to this function.  Seems to work at the moment.
    # Will almost certainly break later.
    if data == '\n':
      self.done_line()
    else:
      self.put_token(data, None)


def process(fn):
  infp = open(fn, 'r')
  builder = IndexBuildParse()
  builder.parse(infp)

  infp.seek(0)
  outfp = open(fn + '.new', 'w')
  inserter = IndexInsertParse(builder.index, outfp)
  inserter.parse(infp)

  infp.close()
  outfp.close()
  os.rename(fn, fn + '.toctool-backup~')
  os.rename(fn + '.new', fn)


def main():
  for fn in sys.argv[1:]:
    process(fn)


if __name__ == '__main__':
  main()
