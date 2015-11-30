#!/usr/bin/env python3

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

"""
backport.status - library for parsing and unparsing STATUS files
"""

# Recipe for interactive testing:
# % python3
# >>> import backport.status
# >>> sf = backport.status.StatusFile(open('STATUS'))
# >>> entries = [p.entry() for p in sf.entries_paras()]
# >>> entries[0]
# <backport.status.StatusEntry object at 0x1b88f90>
# >>>

import collections
import hashlib
import io
import logging
import re
import unittest

logger = logging.getLogger(__name__)


class ParseException(Exception):
  pass


class _ParagraphsIterator:
  "A paragraph-based iterator for file-like objects."

  def __init__(self, stream):
    # KISS implementation, since STATUS files are small.
    self.stream = stream
    self.paragraphs = re.compile(r'\n\s*?\n+').split(stream.read())

  def __iter__(self):
    # Ensure there is exactly one trailing newline.
    return iter(para.rstrip('\n') + "\n" for para in self.paragraphs)

class Test_ParagraphsIterator(unittest.TestCase):
  "Unit test for _ParagraphsIterator."
  def test_basic(self):
    stream = io.StringIO('foo\nfoo2\n\n\nbar\n')
    paragraphs = _ParagraphsIterator(stream)
    self.assertEqual(list(paragraphs), ['foo\nfoo2\n', 'bar\n'])


class Kind:
  "The kind of a single physical paragraph of STATUS.  See 'Paragraph'."

  preamble = object()
  section_header = object()
  nomination = object()
  unknown = object()

  # TODO: can avoid the repetition by using the 'enum' module of Python 3.4
  # That will also make repr() useful.
  @classmethod
  def exists(cls, kind):
    return kind in (cls.preamble, cls.section_header,
                    cls.nomination, cls.unknown)

class Paragraph:
  """A single physical paragraph of STATUS, which may be either a nomination
  or something else."""

  def __init__(self, kind, text, entry, containing_section):
    """Constructor.
    
    KIND is one of the Kind.* enumerators.

    TEXT is the physical text in the file, used by unparsing.

    ENTRY is the StatusEntry object, if Kind.nomination, else None.

    CONTAINING_SECTION is the text of the section header this paragraph appears
    within.  (If this paragraph is a section header, this refers to itself.)
    """

    assert Kind.exists(kind)
    assert (entry is not None) == (kind is Kind.nomination)
    self.kind = kind
    self.text = text
    self._entry = entry
    self._containing_section = containing_section

  # Private for _paragraph_is_header()
  _re_equals_line = re.compile(r'^=+$')

  @classmethod
  def is_header(cls, para_text):
    """PARA_TEXT is a single physical paragraph, as a bare multiline string.
    
    If PARA_TEXT is a section header, return the header text; else, return
    False."""
    lines = para_text.split('\n', 2)
    valid = (len(lines) == 3
             and lines[0].endswith(':')
             and cls._re_equals_line.match(lines[1])
             and lines[2] == '')
    if valid:
      header = lines[0].rstrip(':')
      if header:
        return header
    return False

  def entry(self):
    "Validating accessor for ENTRY."
    assert self.kind is Kind.nomination
    return self._entry

  def section(self):
    "Validating accessor for CONTAINING_SECTION."
    assert self.kind is not Kind.preamble
    return self._containing_section

  def approved(self):
    "TRUE if this paragraph is in the approved section, false otherwise."
    assert self.kind 
    # ### backport.pl used to check just .startswith() here.
    return self.section() == "Approved changes"

  def unparse(self, stream):
    "Write this paragraph to STREAM, an open file-like object."
    if self.kind in (Kind.preamble, Kind.section_header, Kind.unknown):
      stream.write(self.text + "\n")
    elif self.kind is Kind.nomination:
      self.entry().unparse(stream)
    else:
      assert False, "Unknown paragraph kind"

  def __repr__(self):
    return "<Paragraph({!r}, {!r}, {!r}, {!r})>".format(
        self.kind, self.text, self._entry, self._containing_section
    )


class StatusFile:
  "Encapsulates the STATUS file."

  def __init__(self, status_fp):
    "Constructor.  STATUS_FP is an open file-like object to parse."
    self._parse(status_fp)
    self.validate_unique_entry_ids() # Use-case for making this optional?
    self._project_root_url = '^/subversion'

  def branch_url(self, branch_basename):
    """Return the URL of a branch with a given basename, of 'Branch:' headers
    that specify a basename only.

    The returned URL may be an ^/foo short URL."""
    return (self._project_root_url + "/branches/" + branch_basename)

  def trunk_url(self):
    """Return the URL to trunk.  Trunk is used as the default merge source.

    The returned URL may be an ^/foo short URL."""
    return self._project_root_url + '/trunk'

  def _parse(self, status_fp):
    "Parse self.status_fp into self.paragraphs."

    self.paragraphs = []
    last_header = None
    for para_text in _ParagraphsIterator(status_fp):
      kind = None
      entry = None
      header = Paragraph.is_header(para_text)
      if para_text.isspace():
        continue
      elif header:
        kind = Kind.section_header
        last_header = header
      elif last_header is not None:
        try:
          entry = StatusEntry(para_text, status_file=self)
          kind = Kind.nomination
        except ParseException:
          kind = Kind.unknown
          logger.warning("Failed to parse entry {!r} in {!r}".format(
                          para_text, status_fp))
      else:
        kind = Kind.preamble

      self.paragraphs.append(Paragraph(kind, para_text, entry, last_header))

  def entries_paras(self):
    "Return an iterator over entries"
    return filter(lambda para: para.kind is Kind.nomination,
                  self.paragraphs)

  def validate_unique_entry_ids(self):
    # TODO: what about [r42, r43] and [r41, r43] entry pairs?
    """Check if two entries have the same id.  If so, mark them both
    inoperative."""

    # Build an auxiliary data structure.
    id2entry = collections.defaultdict(list)
    for para in self.entries_paras():
      entry = para.entry()
      id2entry[entry.id()].append(para)

    # Examine it for problems.
    for entry_id, entry_paras in id2entry.items():
      if len(entry_paras) != 1:
        # Found a problem.
        #
        # Warn about it, and ignore all involved entries.
        logger.warning("There is more than one {} entry; ignoring them in "
                       "further processing".format(entry_id))
        for para in entry_paras:
          para.kind = Kind.unknown

  def remove(self, entry):
    "Remove ENTRY from SELF."
    for para in self.entries_paras():
      if para.entry() is entry:
        self.paragraphs.remove(para)
        return
    else:
      assert False, "Attempted to remove non-existent entry"

  def unparse(self, stream):
    "Write the STATUS file to STREAM, an open file-like object."
    for para in self.paragraphs:
      para.unparse(stream)


class Test_StatusFile(unittest.TestCase):
  def test__paragraph_is_header(self):
    self.assertTrue(Paragraph.is_header("Nominations:\n========\n"))
    self.assertFalse(Paragraph.is_header("Status of 1.9.12:\n"))

  def test_parse_unparse(self):
    s = (
        "*** This release stream is used for testing. ***\n"
        "\n"
        "Candidate changes:\n"
        "==================\n"
        "\n"
        " * r42\n"
        "   Bump version number to 1.0.\n"
        "   Votes:\n"
        "     +1: jrandom\n"
        "\n"
        "Approved changes:\n"
        "=================\n"
        "\n"
        "This paragraph will trigger an exception.\n"
        "\n"
        " * r43\n"
        "   Bump version number to 1.0.\n"
        "   Votes:\n"
        "     +1: jrandom\n"
        "\n"
    )
    test_file = io.StringIO(s)
    with test_file:
      with self.assertLogs() as cm:
        sf = StatusFile(test_file)
        self.assertRegex(cm.output[0], "Failed to parse.*'.*will trigger.*'")

    self.assertSequenceEqual(
        tuple(para.kind for para in sf.paragraphs),
        (Kind.preamble,
         Kind.section_header, Kind.nomination,
         Kind.section_header, Kind.unknown, Kind.nomination)
    )
    self.assertFalse(sf.paragraphs[1].approved()) # header
    self.assertFalse(sf.paragraphs[2].approved()) # nomination
    self.assertTrue(sf.paragraphs[3].approved()) # header
    self.assertTrue(sf.paragraphs[4].approved()) # unknown

    self.assertIs(sf.paragraphs[2].entry().status_file, sf)

    output_file = io.StringIO()
    sf.unparse(output_file)
    self.assertEqual(s, output_file.getvalue())

  def test_double_nomination(self):
    "Test two nominations of the same group"

    test_file = io.StringIO(
        "Approved changes:\n"
        "=================\n"
        "\n"
        " * r42\n"
        "   First time.\n"
        "\n"
        " * r42\n"
        "   Second time.\n"
        "\n"
    )

    with test_file:
      with self.assertLogs() as cm:
        sf = StatusFile(test_file)
        self.assertRegex(cm.output[0], "There is more than one r42 entry")
        self.assertIs(sf.paragraphs[1].kind, Kind.unknown)
        self.assertIs(sf.paragraphs[2].kind, Kind.unknown)


class StatusEntry:
  """Encapsulates a single nomination.

  An Entry has the following attributes:

  branch - the backport branch's basename, or None.
  revisions - the revisions to nominated, as iterable of int.
  logsummary - the text before the justification, as an array of lines.
  depends - true if a "Depends:" entry was found, False otherwise.
  accept - the value to pass to 'svn merge --accept=%s', or None.
  votes_str - everything after the "Votes:" subheader.  An unparsed string.
  """

  def __init__(self, para_text, status_file=None):
    """Parse an entry from PARA_TEXT, and add it to SELF.  PARA_TEXT must
    contain exactly one entry, as a single multiline string.
    
    STATUS_FILE is the StatusFile object containing this entry, if any.
    """
    self.branch = None
    self.revisions = []
    self.logsummary = []
    self.depends = False
    self.accept = None
    self.votes_str = None
    self.status_file = status_file

    self.raw = para_text

    _re_entry_indentation = re.compile(r'^( *\* )')
    _re_revisions_line = re.compile(r'^(?:r?\d+[,; ]*)+$')

    lines = para_text.rstrip().split('\n')

    # Strip indentation and trailing whitespace.
    match = _re_entry_indentation.match(lines[0])
    if not match:
      raise ParseException("Entry found with no ' * ' line")
    indentation = len(match.group(1))
    lines = (line[indentation:] for line in lines)
    lines = (line.rstrip() for line in lines)

    # Consume the generator.
    lines = list(lines)

    # Parse the revisions lines.
    match = re.compile(r'(\S*) branch|branches/(\S*)').search(lines[0])
    if match:
      # Parse whichever group matched.
      self.branch = self.parse_branch(match.group(1) or match.group(2))
    else:
      while _re_revisions_line.match(lines[0]):
        self.revisions.extend(map(int, re.compile(r'(\d+)').findall(lines[0])))
        lines = lines[1:]

    # Validate it now, since later exceptions rely on it.
    if not(self.branch or self.revisions):
      raise ParseException("Entry found with neither branch nor revisions")

    # Parse the logsummary.
    while lines and not self._is_subheader(lines[0]):
      self.logsummary.append(lines[0])
      lines = lines[1:]

    # Parse votes.
    if "Votes:" in lines:
      index = lines.index("Votes:")
      self.votes_str = '\n'.join(lines[index+1:]) + '\n'
      lines = lines[:index]
      del index
    else:
      self.votes_str = None

    # depends, branch, notes
    while lines:

      if lines[0].strip().startswith('Depends:'):
        self.depends = True
        lines = lines[1:]
        continue

      if lines[0].strip().startswith('Branch:'):
        maybe_value = lines[0].strip().split(':', 1)[1]
        if maybe_value.strip():
          # Value on same line as header
          self.branch = self.parse_branch(maybe_value)
          lines = lines[1:]
          continue
        else:
          # Value should be on next line
          if len(lines) == 1:
            raise ParseException("'Branch:' header found without value")
          self.branch = self.parse_branch(lines[1])
          lines = lines[2:]
          continue

      if lines[0].strip().startswith('Notes:'):
        notes = lines[0].strip().split(':', 1)[1] + "\n"
        lines = lines[1:]

        # Consume the indented body of the "Notes" field.
        while lines and not lines[0][0].isalnum():
          notes += lines[0] + "\n"
          lines = lines[1:]

        # Look for possible --accept directives.
        matches = re.compile(r'--accept[ =]([a-z-]+)').findall(notes)
        if len(matches) > 1:
          raise ParseException("Too many --accept values at %s" % (self,))
        elif len(matches) == 1:
          self.accept = matches[0]

        continue

      # else
      lines = lines[1:]
      continue

    # Some sanity checks.
    if self.branch and self.accept:
      raise ParseException("Entry %s has both --accept and branch" % (self,))

    if not self.logsummary:
      raise ParseException("No logsummary at %s" % (self,))

  def digest(self):
    """Return a unique digest of this entry, with the following property: any
    change to the entry will cause the digest value to change."""

    # Digest the raw text, canonicalizing the number of trailing newlines.
    # There is no particular reason to use md5 over anything else, except for
    # compatibility with existing .backports1 files in people's working copies.
    return hashlib.md5(self.raw.rstrip('\n').encode('UTF-8')
                       + b"\n\n").hexdigest()

  @staticmethod
  def parse_branch(string):
    "Extract a branch name from STRING."
    return string.strip().rstrip('/').split('/')[-1]

  def valid(self):
    "Test the invariants."
    return all([
      self.branch or self.revisions,
      self.logsummary,
      not(self.branch and self.accept),
    ])

  def id(self):
    "Return the first revision or branch's name."
    # Assert a minimal invariant, since this is used by error paths.
    assert self.branch or self.revisions
    if self.branch is not None:
      return self.branch
    else:
      return "r{:d}".format(self.revisions[0])

  def noun(self, start_of_sentence=False):
    """Return a noun phrase describing this entry.
    START_OF_SENTENCE is used to correctly capitalize the result."""
    # Assert a minimal invariant, since this is used by error paths.
    assert self.branch or self.revisions
    if start_of_sentence:
      the = "The"
    else:
      the = "the"
    if self.branch is not None:
      return "{} {} branch".format(the, self.branch)
    elif len(self.revisions) == 1:
      return "r{:d}".format(self.revisions[0])
    else:
      return "{} r{:d} group".format(the, self.revisions[0])

  def logsummarysummary(self):
    "Return a one-line summary of the changeset."
    assert self.valid()
    suffix = "" if len(self.logsummary) == 1 else " [...]"
    return self.logsummary[0] + suffix
             
  # Private for is_vetoed()
  _re_vetoed = re.compile(r'^\s*(-1:|-1\s*[()])', re.MULTILINE)
  def is_vetoed(self):
    "Return TRUE iff a -1 vote has been cast."
    return self._re_vetoed.search(self.votes_str)

  @staticmethod
  def _is_subheader(string):
    """Given a single line from an entry, is that line a subheader (such as
    "Justification:" or "Notes:")?"""
    # TODO: maybe change the 'subheader' heuristic?  Perhaps "line starts with
    # an uppercase letter and ends with a colon".
    #
    # This is currently only used for finding the end of logsummary, and all
    # explicitly special-cased headers (e.g., "Depends:") match this, though.
    return re.compile(r'^\s*[A-Z]\w*:').match(string)

  def unparse(self, stream):
    "Write this entry to STREAM, an open file-like object."
    # For now, this is simple.. until we add interactive editing.
    stream.write(self.raw + "\n")

class Test_StatusEntry(unittest.TestCase):
  def test___init__(self):
    "Test the entry parser"

    # All these entries actually have a "four spaces" line as their last line,
    # but the parser doesn't care.

    s = """\
      * r42, r43,
        r44
        This is the logsummary.
        Branch:
          1.8.x-rfourty-two
        Votes:
          +1: jrandom
    """
    entry = StatusEntry(s)
    self.assertEqual(entry.branch, "1.8.x-rfourty-two")
    self.assertEqual(entry.revisions, [42, 43, 44])
    self.assertEqual(entry.logsummary, ["This is the logsummary."])
    self.assertEqual(entry.logsummarysummary(), "This is the logsummary.")
    self.assertFalse(entry.depends)
    self.assertIsNone(entry.accept)
    self.assertIn("+1: jrandom", entry.votes_str)
    self.assertFalse(entry.is_vetoed())
    self.assertEqual(entry.id(), "1.8.x-rfourty-two")
    self.assertEqual(entry.noun(True), "The 1.8.x-rfourty-two branch")
    self.assertEqual(entry.noun(), "the 1.8.x-rfourty-two branch")

    s = """\
      * r42
        This is the logsummary.
        It has multiple lines.
        Depends: must be merged before the r43 entry"
        Notes:
          Merge with --accept=theirs-conflict.
        Votes:
          +1: jrandom
          -1: jconstant
    """
    entry = StatusEntry(s)
    self.assertIsNone(entry.branch)
    self.assertEqual(entry.revisions, [42])
    self.assertEqual(entry.logsummary,
                     ["This is the logsummary.",
                      "It has multiple lines."])
    self.assertEqual(entry.logsummarysummary(),
                     "This is the logsummary. [...]")
    self.assertTrue(entry.depends)
    self.assertEqual(entry.accept, "theirs-conflict")
    self.assertRegex(entry.votes_str, "(?s)jrandom.*jconstant") # re.DOTALL
    self.assertTrue(entry.is_vetoed())
    self.assertEqual(entry.id(), "r42")
    self.assertEqual(entry.noun(), "r42")

    s = """\
      * ^/subversion/branches/1.8.x-fixes
        This is the logsummary.
        Votes:
          +1: jrandom
          -1 (see <message-id>): jconstant
    """
    entry = StatusEntry(s)
    self.assertEqual(entry.branch, "1.8.x-fixes")
    self.assertEqual(entry.revisions, [])
    self.assertTrue(entry.is_vetoed())

    s = """\
      * r42
        This is the logsummary.
        Branch: ^/subversion/branches/on-the-same-line
        Votes:
          +1: jrandom
    """
    entry = StatusEntry(s)
    self.assertEqual(entry.branch, "on-the-same-line")
    self.assertEqual(entry.revisions, [42])

    self.assertTrue(str(entry)) # tests __str__
    self.assertEqual(entry.raw, s)

    s = """\
      * The 1.8.x-fixes branch
        This is the logsummary.
        Votes:
          +1: jrandom
    """
    entry = StatusEntry(s)
    self.assertEqual(entry.branch, "1.8.x-fixes")

    s = """\
      * The 1.8.x-fixes branch
        This is the logsummary.
        Notes: merge with --accept=tc.
        Votes:
          +1: jrandom
    """
    with self.assertRaisesRegex(ParseException, "both.*accept.*branch"):
      entry = StatusEntry(s)

    s = """\
      * r42
        Votes:
          +1: jrandom
    """
    with self.assertRaisesRegex(ParseException, "No logsummary"):
      entry = StatusEntry(s)

    s = """\
      * r42
        This is the logsummary.
        Notes: merge with --accept=mc.
          This tests multi-line notes.
          Merge with --accept=tc.
        Votes:
          +1: jrandom
    """
    with self.assertRaisesRegex(ParseException, "Too many.*--accept"):
      entry = StatusEntry(s)

    # logsummary that resembles a subheader
    s = """\
      * r42
        svnversion: Fix typo in output.
        Justification:
          Fixes output that scripts depend on.
        Votes:
          +1: jrandom
    """
    entry = StatusEntry(s)
    self.assertEqual(entry.revisions, [42])
    self.assertEqual(entry.logsummary, ["svnversion: Fix typo in output."])

  def test_digest(self):
    s = """\
      * r42
        Fix a bug.
        Votes:
          +1: jrandom\n"""
    digest = '92812e1f36a33f7d51670f89134ad2ee'
    entry = StatusEntry(s)
    self.assertEqual(entry.digest(), digest)

    entry = StatusEntry(s + "\n\n\n")
    self.assertEqual(entry.digest(), digest)

    entry = StatusEntry(s.replace('Fix', 'Introduce'))
    self.assertNotEqual(entry.digest(), digest)

  def test_parse_branch(self):
    inputs = (
      "1.8.x-r42",
      "branches/1.8.x-r42",
      "branches/1.8.x-r42/",
      "subversion/branches/1.8.x-r42",
      "subversion/branches/1.8.x-r42/",
      "^/subversion/branches/1.8.x-r42",
      "^/subversion/branches/1.8.x-r42/",
    )

    for string in inputs:
      self.assertEqual(StatusEntry.parse_branch(string), "1.8.x-r42")

  def test__is_subheader(self):
    "Test that all explicitly-special-cased headers are detected as subheaders."
    subheaders = "Justification: Notes: Depends: Branch: Votes:".split()
    for subheader in subheaders:
      self.assertTrue(StatusEntry._is_subheader(subheader))
      self.assertTrue(StatusEntry._is_subheader(subheader + " with value"))


def setUpModule():
  "Set-up function, invoked by 'python -m unittest'."
  # Suppress warnings generated by the test data.
  # TODO: some test functions assume .assertLogs is available, they fail with
  # AttributeError if it's absent (e.g., on python < 3.4).
  try:
    unittest.TestCase.assertLogs
  except AttributeError:
    logger.setLevel(logging.ERROR)

if __name__ == '__main__':
  unittest.main()
