#!/usr/bin/env python
# Copyright (c) 2006, 2007 by John Szakmeister <john at szakmeister dot net>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

import os
import optparse
import sys
import re


# A handy constant for refering to the NULL digest (one that
# matches every digest).
NULL_DIGEST = '00000000000000000000000000000000'


class FsfsVerifyException(Exception):
  pass


class PotentiallyFixableException(FsfsVerifyException):
  '''Represents a class of problems that we may be able to fix.'''

  def __init__(self, message, offset):
    FsfsVerifyException.__init__(self, message)
    self.offset = offset


class InvalidInstruction(PotentiallyFixableException):
  pass


class InvalidCompressedStream(PotentiallyFixableException):
  pass


class InvalidRepHeader(PotentiallyFixableException):
  pass


class InvalidWindow(PotentiallyFixableException):
  pass


class InvalidSvndiffVersion(FsfsVerifyException):
  pass


class InvalidSvndiffHeader(FsfsVerifyException):
  pass


class DataCorrupt(FsfsVerifyException):
  pass


class NoMoreData(FsfsVerifyException):
  pass


LOG_INSTRUCTIONS = 1
LOG_WINDOWS = 2
LOG_SVNDIFF = 4

LOG_MASK = LOG_SVNDIFF


def log(type, indent, format, *args):
  if type & LOG_MASK:
    indentStr = ' ' * indent
    str = format % args
    str = '\n'.join([indentStr + x for x in str.split('\n')])
    print str


class ByteStream(object):
  def __init__(self, fileobj):
    self._f = fileobj

  def readByte(self):
    return ord(self._f.read(1))

  def tell(self):
    return self._f.tell()

  def advance(self, numBytes):
    self._f.seek(numBytes, 1)

  def clone(self):
    if hasattr(self._f, 'clone'):
      newFileObj = self._f.clone()
    else:
      # We expect the file object to map to a real file
      #
      # Tried using dup(), but (at least on the mac), that ends up
      # creating 2 handles to the same underlying os file object,
      # instead of two independent file objects.  So, we resort to
      # an open call to create a new file object
      newFileObj = open(self._f.name, 'rb')
      newFileObj.seek(self._f.tell())
    return ByteStream(newFileObj)

  # The following let ByteStream behave as a file within the
  # context of this script.

  def read(self, *args, **kwargs):
    return self._f.read(*args, **kwargs)

  def seek(self, *args, **kwargs):
    return self._f.seek(*args, **kwargs)


class ZlibByteStream(ByteStream):
  def __init__(self, fileobj, length):
    self._f = fileobj

    # Store the number of bytes consumed thus far so we can compute an offset
    self._numBytesConsumed = 0

    self._startingOffset = self._f.tell()

    import zlib, binascii
    self._z = zlib.decompressobj(15)

    self._buffer = self._z.decompress(self._f.read(length))
    self._origBufferLength = len(self._buffer)

  def readByte(self):
    if not self._buffer:
      raise NoMoreData, "Unexpected end of data stream!"

    byte = self._buffer[0]
    self._buffer = self._buffer[1:]

    return ord(byte)

  def tell(self):
    return self._origBufferLength - len(self._buffer)

  def advance(self, numBytes):
    while numBytes:
      self.readByte()

  def clone(self):
    if hasattr(self._f, 'clone'):
      newFileObj = self._f.clone()
    else:
      newFileObj = open(self._f.name, 'rb')
      newFileObj.seek(self._f.tell())
    return ByteStream(newFileObj)

  # The following let ByteStream behave as a file within the
  # context of this script.

  def read(self, *args, **kwargs):
    raise

  def seek(self, *args, **kwargs):
    raise


def getVarint(byteStream):
  '''Grabs a variable sized int from a bitstream (meaning this function
  doesn't seek).'''

  i = long(0)
  while True:
    byte = byteStream.readByte()
    i = (i << 7) + (byte & 0x7F)
    if byte & 0x80 == 0:
      break
  return i


INSTR_COPY_SOURCE = 'copy-source'
INSTR_COPY_TARGET = 'copy-target'
INSTR_COPY_DATA = 'copy-data'


class SvndiffInstruction(object):
  def __init__(self, byteStream):
    self.instrOffset = byteStream.tell()

    byte = byteStream.readByte()

    instruction = (byte >> 6) & 3
    length = byte & 0x3F

    if instruction == 3:
      raise InvalidInstruction(
        "Invalid instruction found at offset %d (%02X)" % (self.instrOffset,
                                                           byte),
        self.instrOffset)

    if instruction == 0:
      self.type = INSTR_COPY_SOURCE
    elif instruction == 1:
      self.type = INSTR_COPY_TARGET
    else:
      self.type = INSTR_COPY_DATA

    if length == 0:
      # Length is coded as a varint following the current byte
      length = getVarint(byteStream)


    self.length = length

    if (self.type == INSTR_COPY_SOURCE) or (self.type == INSTR_COPY_TARGET):
      self.offset = getVarint(byteStream)

    if self.type == INSTR_COPY_SOURCE:
      self.sourceOffset = self.offset
    else:
      self.sourceOffset = 0

    if self.type == INSTR_COPY_TARGET:
      self.targetOffset = self.offset
    else:
      self.targetOffset = 0

    # Determine the number of bytes consumed in the source stream, target
    # stream, and the data stream

    if self.type == INSTR_COPY_SOURCE:
      self.sourceLength = self.length
    else:
      self.sourceLength = 0

    if self.type == INSTR_COPY_TARGET:
      self.targetLength = self.length
    else:
      self.targetLength = 0

    if self.type == INSTR_COPY_DATA:
      self.dataLength = self.length
    else:
      self.dataLength = 0

    self.instrLength = byteStream.tell() - self.instrOffset

  def __repr__(self):
    return '<SvndiffInstruction %s so:%d sl:%d to: %d tl:%d dl:%d (%d, %d)>' % (
      self.type, self.sourceOffset, self.sourceLength, self.targetOffset,
      self.targetLength, self.dataLength, self.instrOffset, self.instrLength)


class Window(object):
  def __init__(self, byteStream, svndiffVersion):
    if svndiffVersion not in [0, 1]:
      raise InvalidSvndiffVersion, \
        "Invalid svndiff version %d" % svndiffVersion

    # Record the initial offset of the window
    self.windowOffset = byteStream.tell()

    try:
      self.sourceOffset = getVarint(byteStream)
      self.sourceLength = getVarint(byteStream)
      self.targetLength = getVarint(byteStream)
      self.instrLength = getVarint(byteStream)
      self.dataLength = getVarint(byteStream)
      self.windowHeaderLength = byteStream.tell() - self.windowOffset
      self.windowLength = \
        self.windowHeaderLength + self.instrLength + self.dataLength

      # Store the byte stream, and clone it for use as a data stream.
      self.instrByteStream = byteStream
      self.dataByteStream = byteStream.clone()

      # Advance the data stream past the instructions to the start of the data.
      self.dataByteStream.advance(self.instrLength)
    except:
      e = InvalidWindow(
        "The window header at offset %d appears to be corrupted" % \
          (self.windowOffset),
        self.windowOffset)
      e.windowOffset = self.windowOffset
      raise e


    # In svndiff1, the instruction area starts with a varint-encoded length.
    # If this length matches the one encoded in the header, then there is no
    # compression.  If it differs, then the stream is compressed with zlib.

    self.origInstrStream = self.instrByteStream
    self.origDataStream = self.dataByteStream
    self.isInstrCompressed = False
    self.isDataCompressed = False
    self.compressedInstrLength = self.instrLength
    self.compressedDataLength = self.dataLength

    if svndiffVersion == 1:
      try:
        offset = self.instrByteStream.tell()
        encodedInstrLength = getVarint(self.instrByteStream)
        instrIntSize = self.instrByteStream.tell() - offset

        offset = self.dataByteStream.tell()
        encodedDataLength = getVarint(self.dataByteStream)
        dataIntSize = self.dataByteStream.tell() - offset

        self.instrLength = encodedInstrLength
        self.dataLength = encodedDataLength
      except:
        e = InvalidWindow(
          "The window header at offset %d appears to be corrupted" % \
            (self.windowOffset),
          self.windowOffset)
        e.windowOffset = self.windowOffset
        raise e

      # Now, we need to make a determination about whether the data and
      # instructions are compressed.  If they are, we need to zlib decompress
      # them.  We do that by creating another stream and that will decompress
      # the data on the fly.
      try:
        offset = self.instrByteStream.tell()
        if self.compressedInstrLength - instrIntSize != self.instrLength:
          self.origInstrStream = self.instrByteStream
          self.instrByteStream = ZlibByteStream(self.origInstrStream,
                                                self.compressedInstrLength)
          self.isInstrCompressed = True
      except Exception, e:
        new_e = InvalidCompressedStream(
          "Invalid compressed instr stream at offset %d (%s)" % (offset,
                                                                 str(e)),
          offset)
        new_e.windowOffset = self.windowOffset
        raise new_e

      try:
        offset = self.dataByteStream.tell()
        if self.compressedDataLength - dataIntSize != self.dataLength:
          self.origDataStream = self.dataByteStream
          self.dataByteStream = ZlibByteStream(self.origDataStream,
                                               self.compressedDataLength)
          self.isDataCompressed = True
      except Exception, e:
        new_e = InvalidCompressedStream(
          "Invalid compressed data stream at offset %d (%s)" % (offset,
                                                                str(e)),
          offset)
        new_e.windowOffset = self.windowOffset
        raise new_e

  def verify(self):
    expectedInstrLength = self.instrLength
    expectedDataLength = self.dataLength
    expectedTargetLength = self.targetLength
    expectedSourceLength = self.sourceLength

    computedInstrLength = 0
    computedDataLength = 0
    computedTargetLength = 0
    computedSourceLength = 0

    if expectedInstrLength == 0:
      e = InvalidWindow(
        "Corrupt window (at offset %d) has 0 instructions?!" % self.windowOffset,
        self.windowOffset)
      e.windowOffset = self.windowOffset
      raise e

    while computedInstrLength < expectedInstrLength:
      try:
        instr = SvndiffInstruction(self.instrByteStream)
      except PotentiallyFixableException, e:
        e.window = self
        e.windowOffset = self.windowOffset
        raise

      log(LOG_INSTRUCTIONS, 4, repr(instr))

      computedInstrLength += instr.instrLength
      computedDataLength += instr.dataLength
      computedSourceLength += instr.sourceLength
      computedTargetLength += \
        instr.targetLength + instr.sourceLength + instr.dataLength

    if computedInstrLength != expectedInstrLength:
      e = InvalidWindow(
        "The number of instruction bytes consumed (%d) doesn't match the expected number (%d)" % \
          (computedInstrLength, expectedInstrLength),
        self.windowOffset)
      e.windowOffset = self.windowOffset
      raise e

    if computedDataLength != expectedDataLength:
      e = InvalidWindow(
        "The number of data bytes consumed (%d) doesn't match the expected number (%d)" % \
          (computedDataLength, expectedDataLength),
        self.windowOffset)
      e.windowOffset = self.windowOffset
      raise e

    if computedTargetLength != expectedTargetLength:
      e = InvalidWindow(
        "The number of target bytes consumed (%d) doesn't match the expected number (%d)" % \
          (computedTargetLength, expectedTargetLength),
        self.windowOffset)
      e.windowOffset = self.windowOffset
      raise e

    # It appears that the source length specified in the window, isn't exactly
    # equal to what gets consumed.  I suspect that's because the algorithm is using different
    # offsets within the window, and one offset/length pair will reach the end of the window.
    # However, this hasn't shown to be a clear indicator of corruption.  So for now, I'm
    # commenting it out.
    #
    #if computedSourceLength != expectedSourceLength:
    #  e = InvalidWindow(
    #    "The number of source bytes consumed (%d) doesn't match the expected number (%d)" % \
    #      (computedSourceLength, expectedSourceLength),
    #    self.windowOffset)
    #  e.windowOffset = self.windowOffset
    #  raise e

    # Advance past the data.  We do this using seek because we might have
    # read a few bytes from the stream if it potentially had compressed data
    self.origInstrStream.seek(self.windowOffset + self.windowLength)

  def __repr__(self):
    if hasattr(self, 'compressedInstrLength'):
      str = 'cil: %d cdl: %d ' % (self.compressedInstrLength,
                                  self.compressedDataLength)
    else:
      str = ''

    return "<Window wo:%d so:%d sl:%d tl:%d %sil:%d dl:%d whl:%d wl:%d>" % (
      self.windowOffset, self.sourceOffset, self.sourceLength,
      self.targetLength, str, self.instrLength, self.dataLength,
      self.windowHeaderLength, self.windowLength)


class Svndiff(object):
  def __init__(self, fileobj, length):
    self._f = fileobj
    self.startingOffset = self._f.tell()

    header = self._f.read(4)
    if len(header) != 4:
      raise EOFError, \
        "Unexpected end of file while svndiff header at offset %d)" % \
        (self._f.tell())

    if header[0:3] != 'SVN':
      raise InvalidSvndiffHeader, "Invalid svndiff header at offset %d" % \
      (self.startingOffset)

    self.version = ord(header[3])
    if self.version not in [0, 1]:
      raise InvalidSvndiffVersion, "Invalid svndiff version %d" % self.version

    self._length = length - 4

  def verify(self):
    self._f.seek(self.startingOffset+4)

    bs = ByteStream(self._f)

    log(LOG_SVNDIFF, 2, "<Svndiff so: %d ver: %d>", self.startingOffset,
        self.version)

    try:
      remaining = self._length
      while remaining > 0:
        w = Window(bs, self.version)
        log(LOG_WINDOWS, 3, repr(w))
        w.verify()
        remaining -= w.windowLength
    except PotentiallyFixableException, e:
      e.svndiffStart = self.startingOffset
      raise


def getDirHash(f):
  l = f.readline()
  if l != 'PLAIN\n':
    raise ValueError, "Expected a PLAIN representation (%d)" % f.tell()

  hash = {}

  while True:
    field = f.readline()[:-1]
    if field == 'END':
      break
    assert(field[0] == 'K')
    length = int(field.split(' ')[1])
    field = f.readline()[:length]

    value = f.readline()[:-1]
    assert(value[0] == 'V')
    length = int(value.split(' ')[1])
    value = f.readline()[:length]

    (type, txn) = value.split(' ')
    hash[field] = [NodeType(type), NodeId(txn)]

  return hash



class Rep(object):
  def __init__(self, type, rev, offset, length, size, digest,
               contentType, currentRev, noderev):
    self.type = type
    self.rev = int(rev)
    self.offset = int(offset)
    self.length = int(length)
    self.size = int(size)

    self.digest = digest
    self.currentRev = currentRev

    self.contentType = contentType
    self.noderev = noderev

  def __repr__(self):
    if not self.contentType:
      contentType = 'UNKNOWN'
    else:
      if self.contentType not in ['PLAIN', 'DELTA', None]:
        contentType = 'INVALID'
      else:
        contentType = self.contentType
    return '%s: %s %d %d %d %d %s' % (self.type, contentType, self.rev,
                                      self.offset, self.length, self.size,
                                      self.digest)

  def verify(self, f, dumpInstructions, dumpWindows):
    if self.contentType not in ['PLAIN', 'DELTA', None]:
      e = InvalidRepHeader("Invalid rep header found at %d (%s)!" % \
                                     (self.offset, self.contentType),
                           self.offset)
      e.rep = self
      e.noderev = self.noderev
      raise e

    if self.rev != currentRev:
      print >>sys.stderr, "Skipping text rep since it isn't present in the current rev"
      return

    f.seek(self.offset)
    header = f.read(5)
    if header != self.contentType:
      raise FsfsVerifyException, \
        "Invalid rep header found at %d (%s, %s)!" % (self.offset, header,
                                                      self.contentType)

    if header == 'DELTA':
      # Consume the rest of the DELTA header
      while f.read(1) != '\n':
        pass

      # This should be the start of the svndiff stream
      actual_start = f.tell()
      try:
        svndiff = Svndiff(f, self.length)
        svndiff.verify()
        digest = None
      except Exception, e:
        e.rep = self
        e.noderev = self.noderev
        raise

      if digest and (self.digest != NULL_DIGEST):
        assert(digest == self.digest)
    else:
      if f.read(1) != '\n':
        raise DataCorrupt, "Expected a '\\n' after PLAIN"

      import md5
      m = md5.new()
      m.update(f.read(self.length))

      if self.digest and self.digest != NULL_DIGEST \
          and self.digest != m.hexdigest():
        raise DataCorrupt, \
          "PLAIN data is corrupted.  Expected digest '%s', computed '%s'." % (
            self.digest, m.hexdigest())

      if f.read(7) != 'ENDREP\n':
        raise DataCorrupt, "Terminating ENDREP missing!"


class TextRep(Rep):
  def __init__(self, rev, offset, length, size, digest,
               contentType, currentRev, noderev):
    super(TextRep,self).__init__('text', rev, offset, length, size,
                                 digest, contentType, currentRev, noderev)


class PropRep(Rep):
  def __init__(self, rev, offset, length, size, digest,
               contentType, currentRev, noderev):
    super(PropRep,self).__init__('prop', rev, offset, length, size,
                                 digest, contentType, currentRev, noderev)


class NodeId(object):
  def __init__(self, nodeid):
    (self.txn_name, offset) = nodeid.split('/')
    self.offset = int(offset)
    self.rev = int(self.txn_name.split('.')[2][1:])

  def __repr__(self):
    return self.txn_name + '/%d' % self.offset

  def __eq__ (self, other):
    s = self.txn_name + '/%d' % self.offset
    if s == other:
      return True

    return False


class NodeType(object):
  def __init__(self, t):
    if (t != 'file') and (t != 'dir'):
      raise ValueError, 'Invalid Node type received: "%s"' % t
    self.type = t

  def __repr__(self):
    return self.type[:]


class NodeRev(object):
  def __init__(self, f, currentRev):
    self.pred = None
    self.text = None
    self.props = None
    self.cpath = None
    self.copyroot = None
    self.copyfrom = None
    self.dir = []

    self.nodeOffset = f.tell()

    while True:
      line = f.readline()
      if line == '':
        raise IOError, "Unexpected end of file"
      if line == '\n':
        break

      # break apart the line
      try:
        (field, value) = line.split(':', 1)
      except:
        print repr(line)
        print self.nodeOffset
        print f.tell()
        raise

      # pull of the leading space and trailing new line
      value = value[1:-1]

      if field == 'id':
        self.id = NodeId(value)
      elif field == 'type':
        self.type = NodeType(value)
      elif field == 'pred':
        self.pred = NodeId(value)
      elif field == 'text':
        (rev, offset, length, size, digest) = value.split(' ')
        rev = int(rev)
        offset = int(offset)
        length = int(length)
        size = int(size)

        if rev != currentRev:
          contentType = None
        else:
          savedOffset = f.tell()
          f.seek(offset)
          contentType = f.read(5)
          f.seek(savedOffset)

        self.text = TextRep(rev, offset, length, size, digest,
                            contentType, currentRev, self)
      elif field == 'props':
        (rev, offset, length, size, digest) = value.split(' ')
        rev = int(rev)
        offset = int(offset)
        length = int(length)
        size = int(size)

        if rev != currentRev:
          contentType = None
        else:
          savedOffset = f.tell()
          f.seek(offset)
          contentType = f.read(5)
          f.seek(savedOffset)

        self.props = PropRep(rev, offset, length, size, digest,
                             contentType, currentRev, self)
      elif field == 'cpath':
        self.cpath = value
      elif field == 'copyroot':
        self.copyroot = value
      elif field == 'copyfrom':
        self.copyfrom = value

    if self.type.type == 'dir':
      if self.text:
        if self.id.rev == self.text.rev:
          offset = f.tell()
          f.seek(self.text.offset)
          self.dir = getDirHash(f)
          f.seek(offset)
        else:
          # The directory entries are stored in another file.
          print "Warning: dir entries are stored in rev %d for noderev %s" % (
            self.text.rev, repr(self.id))

  def __repr__(self):
    str = 'NodeRev Id: %s\n type: %s\n' % (repr(self.id), repr(self.type))
    if self.pred:
      str = str + ' pred: %s\n' % repr(self.pred)
    if self.text:
      str = str + ' %s\n' % repr(self.text)
    if self.props:
      str = str + ' %s\n' % repr(self.props)
    if self.cpath:
      str = str + ' cpath: %s\n' % self.cpath
    if self.copyroot:
      str = str + ' copyroot: %s\n' % self.copyroot
    if self.copyfrom:
      str = str + ' copyfrom: %s\n' % self.copyfrom
    if self.dir:
      str = str + ' dir contents:\n'
      for k in self.dir:
        str = str + '  %s: %s\n' % (k, self.dir[k])
    return str[:-1]


class ChangedPaths(object):
  def __init__(self, f):
    self.changedPaths = {}

    while True:
      currentOffset = revFile.tell()
      action = revFile.readline()
      if action == '\n' or action == '':
        break

      path = action[:-1]
      try:
        (id, action, textMod, propMod) = action[:-1].split(' ')[:4]
      except:
        raise DataCorrupt, \
          "Data appears to be corrupt at offset %d" % currentOffset
      path = path[len(' '.join([id, action, textMod, propMod]))+1:]

      line = revFile.readline()
      if line != '\n':
        (copyfromRev, copyfromPath) = line[:-1].split(' ', 1)
      else:
        copyfromRev = -1
        copyfromPath = ''

      self.changedPaths[path] = (id, action, textMod, propMod,
                                 copyfromRev, copyfromPath)


  def __iter__(self):
    return self.changedPaths.iteritems()


def getRootAndChangedPaths(revFile):
  offset = -2
  while True:
    revFile.seek(offset, 2)
    c = revFile.read(1)
    if c == '\n':
      offset = revFile.tell()
      break
    offset = offset - 1

  (rootNode, changedPaths) = map(int, revFile.readline().split(' '))

  return (rootNode, changedPaths)


def dumpChangedPaths(changedPaths):
  print "Changed Path Information:"
  for (path,
       (id, action, textMod, propMod,
        copyfromRev, copyfromPath)) in changedPaths:
    print " %s:" % path
    print "  id: %s" % id
    print "  action: %s" % action
    print "  text mod: %s" % textMod
    print "  prop mod: %s" % propMod
    if copyfromRev != -1:
      print "  copyfrom path: %s" % copyfromPath
      print "  copyfrom rev: %s" % copyfromRev
    print


class WalkStrategy(object):
  def __init__(self, filename, rootOffset, currentRev):
    self.f = open(filename, 'rb')
    self.rootOffset = rootOffset
    self.f.seek(rootOffset)
    self.currentRev = currentRev

  def _nodeWalker(self):
    raise NotImplementedError, "_nodeWalker is not implemented"

  def __iter__(self):
    self.f.seek(self.rootOffset)
    return self._nodeWalker()


class ClassicStrategy(WalkStrategy):
  def _nodeWalker (self):
    noderev = NodeRev(self.f, self.currentRev)
    yield noderev

    if noderev.type.type == 'dir':
      for e in noderev.dir:
        if noderev.dir[e][1].rev == noderev.id.rev:
          self.f.seek(noderev.dir[e][1].offset)
          for x in self._nodeWalker():
            yield x


class RegexpStrategy(WalkStrategy):
  def __init__(self, filename, rootOffset, currentRev):
    WalkStrategy.__init__(self, filename, rootOffset, currentRev)

    # File object passed to the NodeRev() constructor so that it
    # doesn't interfere with our regex search.
    self.nodeFile = open(filename, 'rb')

  def _nodeWalker(self):
    nodeId_re = re.compile(r'^id: [a-z0-9\./]+$')

    self.f.seek(0)
    offset = 0

    for line in self.f:
      match = nodeId_re.search(line)
      if match:
        self.nodeFile.seek(offset)
        noderev = NodeRev(self.nodeFile, self.currentRev)
        yield noderev

      offset = offset + len(line)


def verify(noderev, revFile, dumpInstructions, dumpWindows):
  print noderev

  if noderev.text:
    noderev.text.verify(revFile,
                        dumpInstructions,
                        dumpWindows)

  if noderev.props and noderev.props.rev == noderev.props.currentRev:
    noderev.props.verify(revFile,
                         dumpInstructions,
                         dumpWindows)

  print


def truncate(noderev, revFile):
  txnId = noderev.id

  print "Truncating node %s (%s)" % (txnId, noderev.cpath)

  # Grab the text rep
  textRep = noderev.text

  # Fix the text rep contents
  offset = textRep.offset
  revFile.seek(offset, 0)
  revFile.write("PLAIN\x0aENDREP\x0a")

  # Fix the node rev
  offset = noderev.nodeOffset
  revFile.seek(offset, 0)
  while True:
    savedOffset = revFile.tell()
    s = revFile.readline()
    if s[:4] == 'text':
      revFile.seek(savedOffset, 0)
      break

  line = revFile.readline()
  revFile.seek(savedOffset, 0)
  fields = line.split(' ')
  overallLength = len(line)

  fields[3] = '0' * len(fields[3])
  fields[4] = '0' * len(fields[4])
  fields[5] = 'd41d8cd98f00b204e9800998ecf8427e'
  newTextRep = ' '.join(fields) + '\x0a'
  assert(len(newTextRep) == overallLength)
  revFile.write(newTextRep)
  print "Done."
  sys.exit(0)


def fixHeader(e, revFile):
  '''Attempt to fix the rep header.  e is expected to be of type
  InvalidRepHeader, since the exception stores the necessary information
  to help repair the file.'''

  # First, we need to locate the real start of the text rep
  textrep_re = re.compile(r'^(DELTA( \d+ \d+ \d+)?|PLAIN)$')

  revFile.seek(0)
  offset = 0
  originalOffset = 0
  for line in revFile:
    m = textrep_re.match(line)
    if m:
      if offset >= originalOffset and offset < e.offset:
        originalOffset = offset
        headerLen = len(line)
    offset = offset + len(line)

  print "Original text rep located at", originalOffset

  # Okay, now we have the original offset of the text rep that was
  # in the process of being written out.  The header portion of the
  # text rep has a fsync() done after it, so the 4K blocks actually
  # start after the header.  We need to make sure to copy the header
  # and the next 4K, to be on the safe side.
  copyLen = 4096 + headerLen

  revFile.seek(originalOffset)
  block = revFile.read(copyLen)
  print "Copy %d bytes from offset %d" % (copyLen, originalOffset)

  print "Write %d bytes at offset %d" % (copyLen, e.offset)
  revFile.seek(e.offset)
  revFile.write(block)
  revFile.flush()

  print "Fixed? :-)  Re-run fsfsverify without the -f option"


def fixStream(e, revFile):
  startOffset = e.svndiffStart
  errorOffset = e.windowOffset

  repeatedBlockOffset = errorOffset - ((errorOffset - startOffset) % 4096)

  # Now we need to move up the rest of the rep

  # Determine the final offset by finding the end of the rep.
  revFile.seek(errorOffset)

  endrep_re = re.compile(".*ENDREP$")
  srcLength = 0
  for l in revFile:
    srcLength += len(l)
    m = endrep_re.match(l)
    if m:
      break

  if not m:
    raise "Couldn't find end of rep!"

  finalOffset = errorOffset + srcLength
  srcOffset = errorOffset
  destOffset = repeatedBlockOffset

  print "Copy %d bytes from offset %d" % (srcLength, srcOffset)
  print "Write %d bytes at offset %d" % (srcLength, destOffset)

  while srcOffset < finalOffset:
    blen = 64*1024
    if (finalOffset - srcOffset) < blen:
      blen = finalOffset - srcOffset
    revFile.seek(srcOffset)
    block = revFile.read(blen)
    revFile.seek(destOffset)
    revFile.write(block)

    srcOffset += blen
    destOffset += blen

  revFile.flush()
  revFile.close()

  print "Fixed? :-)  Re-run fsfsverify without the -f option"


def checkOptions(options):
  count = 0
  for k,v in options.__dict__.items():
    if v and (k in ['dumpChanged', 'truncate', 'fixRlle']):
      count = count + 1

  if count > 1:
    print >>sys.stderr, "Please use only one of -c, -f, and -t."
    sys.exit(1)

  if options.dumpChanged and (options.dumpWindows or options.dumpInstructions):
    print >>sys.stderr, \
      "-c is incompatible with -w and -i.  Dropping -w and/or -i."

  if options.noVerify and (options.dumpWindows or options.dumpInstructions):
    print >>sys.stderr, \
      "--no-verify is incompatible with -w and -i.  Dropping -w and/or -i."


def handleError(error, withTraceback=False):
  print
  if withTraceback:
    import traceback
    traceback.print_exc()

  print >>sys.stderr,"Error %s: %s" % (error.__class__.__name__, str(e))
  print >>sys.stderr,"Try running with -f to fix the revision"
  sys.exit(1)


if __name__ == '__main__':
  from optparse import OptionParser

  parser = OptionParser("usage: %prog [-w | -i | -r | -n] REV-FILE")
  parser.add_option("-c", "--changed-paths",
                    action="store_true", dest="dumpChanged",
                    help="Dump changed path information", default=False)
  parser.add_option("", "--no-verify",
                    action="store_true", dest="noVerify",
                    help="Don't parse svndiff streams.", default=False)
  parser.add_option("-i", "--instructions",
                    action="store_true", dest="dumpInstructions",
                    help="Dump instructions (implies -w)", default=False)
  parser.add_option("-w", "--windows",
                    action="store_true", dest="dumpWindows",
                    help="Dump windows", default=False)
  parser.add_option("-n", "--noderev-regexp",
                    action="store_true", dest="noderevRegexp",
                    help="Find all noderevs using a regexp", default=False)
  parser.add_option("-f", "--fix-read-length-line-error",
                    action="store_true", dest="fixRlle",
                    help="Attempt to fix the read length line error",
                    default=False)
  parser.add_option("-t", "--truncate",
                    action="store", type="string", dest="truncate",
                    help="Truncate the specified node rev.",
                    default=None)
  parser.add_option("", "--traceback",
                    action="store_true", dest="showTraceback",
                    help="Show error tracebacks (mainly used for debugging).",
                    default=False)

  (options, args) = parser.parse_args()

  if len(args) != 1:
    print >>sys.stderr, "Please specify exactly one rev file."
    parser.print_help()
    sys.exit(1)

  checkOptions(options)

  filename = args[0]

  if options.dumpInstructions:
    options.dumpWindows = True
    LOG_MASK |= LOG_INSTRUCTIONS

  if options.dumpWindows:
    LOG_MASK |= LOG_WINDOWS

  if options.truncate or options.fixRlle:
    revFile = open(filename, 'r+b')
  else:
    revFile = open(filename, 'rb')

  (root, changed) = getRootAndChangedPaths(revFile)

  if options.dumpChanged:
    revFile.seek(changed)
    changedPaths = ChangedPaths(revFile)

    dumpChangedPaths(changedPaths)
    sys.exit(0)

  try:
    import re
    match = re.match('([0-9]+)', os.path.basename(filename))
    currentRev = int(match.group(1), 10)
  except:
    raise CmdlineError, \
      "The file name must start with a decimal number that indicates the revision"

  if options.noderevRegexp:
    strategy = RegexpStrategy(filename, root, currentRev)
  else:
    strategy = ClassicStrategy(filename, root, currentRev)

  # Make stderr the same as stdout.  This helps when trying to catch all of the
  # output from a run.
  sys.stderr = sys.stdout

  try:
    for noderev in strategy:
      try:
        if options.truncate:
          # Check to see if this is the rev we need to truncate
          if options.truncate == noderev.id:
            truncate(noderev, revFile)

        else:
          print noderev

          if not options.noVerify:
            if noderev.text:
              noderev.text.verify(revFile,
                                  options.dumpInstructions,
                                  options.dumpWindows)

            if noderev.props and noderev.props.rev == noderev.props.currentRev:
              noderev.props.verify(revFile,
                                   options.dumpInstructions,
                                   options.dumpWindows)

          print
      except:
        sys.stdout.flush()
        raise
  except InvalidRepHeader, e:
    if not options.fixRlle:
      handleError(e, options.showTraceback)

    fixHeader(e, revFile)

  except PotentiallyFixableException, e:
    if not options.fixRlle:
      handleError(e, options.showTraceback)

    fixStream(e, revFile)

