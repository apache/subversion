#!/usr/bin/env python

import sys
import os
import re
import glob


lang2LANG = { 'python': 'PYTHON', 'perl5': 'PERL', 'ruby': 'RUBY' }


class Queue:
  type_blank, type_mapbegin, type_mapcont, type_other = range(4)

  def __init__(self, ofp):
    self.queue = []
    self.lang_on_queue = None
    self.ofp = ofp

  def enqueue(self, type, lang, line):
    if lang is not None:
      assert type == self.type_mapbegin
      if self.lang_on_queue != lang:
        self.flush()
        self.lang_on_queue = lang
    if type == self.type_other:
      self.flush()
      self.ofp.write(line)
    else:
      self.queue.append((type, line))

  def flush(self):
    while self.queue and self.queue[0][0] == self.type_blank:
      self.ofp.write(self.queue.pop(0)[1])
    if not self.queue:
      return
    assert (self.queue[0][0] == self.type_mapbegin and
        self.lang_on_queue is not None)
    self.ofp.write('#ifdef SWIG%s\n' % lang2LANG[self.lang_on_queue])
    local_blank_queue = []
    for i in self.queue:
      if i[0] == self.type_blank:
        local_blank_queue.append(i[1])
      else:
        for j in local_blank_queue:
          self.ofp.write(j)
          del local_blank_queue[:]
        self.ofp.write(i[1])
    del self.queue[:]
    self.ofp.write('#endif\n')
    for j in local_blank_queue:
      self.ofp.write(j)
    self.lang_on_queue = None


def process_file(fname):
  old_fname = fname + '.old'
  os.rename(fname, old_fname)
  ifp = open(old_fname, 'r')
  ifpiter = iter(ifp)
  ofp = open(fname, 'w')
  q = Queue(ofp)
  re_blank = re.compile(r'^\s*$')
  re_mapbegin = re.compile(r'(?s)^%typemap\((python|perl5|ruby), ?(.*$)')
  re_mapend = re.compile(r'^(?:}\s*|%typemap.*;)$')
  try:
    for i in ifpiter:
      if re_blank.match(i):
        q.enqueue(q.type_blank, None, i)
        continue
      m = re_mapbegin.match(i)
      if m:
        q.enqueue(q.type_mapbegin, m.group(1), '%typemap('+m.group(2))
        bracketlevel = 0
        any_brackets = False
        while 1:
          bracketlevel += i.count('{')
          if bracketlevel:
            any_brackets = True
          bracketlevel -= i.count('}')
          if not bracketlevel and (any_brackets or i.endswith(';\n')):
            break
          i = ifpiter.next()
          if re_mapbegin.match(i):
            sys.stderr.write('In %r, saw typemap opener before closing '
                'previous!?\n' % fname)
            ofp.write('##### TYPEMAP OPEN FROM HERE NOT CLOSED #####\n')
          q.enqueue(q.type_mapcont, None, i)
        continue
      q.enqueue(q.type_other, None, i)
  except StopIteration:
    sys.stderr.write('In %r, hit EOF inside typemap!?\n' % fname)
    ofp.write('##### EOF HIT WITH TYPEMAP OPEN FROM HERE #####\n')
  q.flush()
  ifp.close()
  ofp.close()


def main():
  action_files = glob.glob('*.i') + glob.glob('include/*.swg')
  for file in action_files:
    process_file(file)


if __name__ == '__main__':
  main()
