#!/usr/bin/env python

"""\
This tool regenerates and replaces the ToC in design.html from the actual
structure of <div>s and <h[2345]>s present in the body of the document.
The section to be overwritten in design.html is identified as the XML subtree
rooted at <ol id="toc">.

Usage: ./toctool.py
"""

import sys
import os
import xml.parsers.expat


def bind_handler_object_to_expat_parser(handlerobj, parser):
  for name in dir(handlerobj): 
    if name.endswith('Handler'):
      setattr(parser, name, getattr(handlerobj, name))


class Index:
  def __init__(self):
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


class IndexBuildHandler:
  keys = {'h2':None, 'h3':None, 'h4':None, 'h5':None}

  def __init__(self):
    self.index = Index()
    self.keyptr = 0
    self.collecting_text = False
    self.text = ''
    self.waiting_for_elt = None
    self.saved_id = None
    self.elt_stack = []

  def StartElementHandler(self, name, attrs):
    if name == 'div':
      cl = attrs.get('class')
      if cl in self.keys:
        self.waiting_for_elt = cl
        self.saved_id = attrs.get('id')
        self.elt_stack.append((name, True))
        return
    if name == self.waiting_for_elt:
      self.waiting_for_elt = None
      self.collecting_text = name
      self.text = ''
    self.elt_stack.append((name, False))

  def EndElementHandler(self, name):
    if self.collecting_text:
      if name == self.collecting_text:
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
  for i in xrange(0, len(l), 2):
    d[l[i]] = l[i+1]
  return d


class IndexInsertHandler:
  do_not_minimize = {'script':None}
  temp_do_not_indent = {'div':None, 'a': None, 'strong': None, 'em': None}

  def __init__(self, index, outfp):
    self.index = index
    self.outfp = outfp
    self._element_open = False
    self.linepos = 0
    self.indentpos = 0
    self.elt_stack = []
    self.skipping_toc = False

  def _finish_pending(self, due_to_end):
    if self._element_open:
      self._element_open = False
      if due_to_end:
        self.linepos += 2
        self.outfp.write('/>')
        return True
      else:
        self.linepos += 1
        self.outfp.write('>')
    return False

  def StartElementHandler(self, name, attrs):
    if self.indentpos == 0:
      self.indentpos = self.linepos
    self._finish_pending(False)
    if name == 'ol' and attrlist_to_dict(attrs).get('id') == 'toc':
      self.outfp.write(self.index.renderXML())
      self.skipping_toc = True
      self.elt_stack.append((name, True))
      return
    if not self.skipping_toc:
      toks = [ "<%s" % name ]
      while attrs:
        aname = attrs.pop(0)
        aval = attrs.pop(0)
        toks.append(' %s="%s"' % (aname, aval))
      for t in toks:
        self.linepos += len(t)
        if self.linepos > 79 and name not in self.temp_do_not_indent:
          self.linepos = len(t) + self.indentpos + 2
          self.outfp.write('\n ' + ' '*self.indentpos)
        self.outfp.write(t)
      self._element_open = True
    self.elt_stack.append((name, False))

  def EndElementHandler(self, name):
    if not self.skipping_toc:
      if self.indentpos == 0:
        self.indentpos = self.linepos
      if not self._finish_pending(name not in self.do_not_minimize):
        data = "</%s>" % name
        self.linepos += len(data)
        self.outfp.write(data)
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
      self.indentpos = self.linepos = 0
    else:
      self.linepos += len(data)
      if self.indentpos == 0:
        i = 0
        while i < len(data) and data[i] == ' ':
          i += 1
        self.indentpos = i
    self.outfp.write(data)


def do_expat_parse(handler, file, ordered_attributes):
  p = xml.parsers.expat.ParserCreate()
  p.ordered_attributes = ordered_attributes
  p.returns_unicode = False
  p.specified_attributes = True
  bind_handler_object_to_expat_parser(handler, p)
  p.ParseFile(file)
  

def main():
  builder = IndexBuildHandler()
  infp = open('design.html', 'r')
  do_expat_parse(builder, infp, False)
  
  infp.seek(0)
  outfp = open('design.html.new', 'w')
  inserter = IndexInsertHandler(builder.index, outfp)
  do_expat_parse(inserter, infp, True)

  infp.close()
  outfp.close()
  os.rename('design.html', 'design.html.toctool-backup~')
  os.rename('design.html.new', 'design.html')


if __name__ == '__main__':
  main()
