#!/usr/bin/env python

# See usage() for details, or run with --help option.
# 
#          .-------------------------------------------------.
#          |  "An ad hoc format deserves an ad hoc parser."  |
#          `-------------------------------------------------'
# 
# Some Subversion project log messages include parseable data to help
# track who's contributing what.  The exact syntax is described in
# hacking.html#crediting, but here's an example, indented by three
# spaces, i.e., the "Patch by:" starts at the beginning of a line:
#
#    Patch by: David Anderson <david.anderson@calixo.net>
#              <justin@erenkrantz.com>
#              me
#    (I wrote the regression tests.)
#    Suggested by: Snosbig Q. Ptermione <sqptermione@example.com>
#    Review by: Justin Erenkrantz <justin@erenkrantz.com>
#               rooneg
#    (They caught an off-by-one error in the main loop.)
#
# This is a pathological example, but it shows all the things we might
# need to parse.  We need to:
#
#   - Detect the officially-approved "WORD by: " fields.
#   - Grab every name (one per line) in each field.
#   - Handle names in various formats, unifying where possible.
#   - Expand "me" to the committer name for this revision.
#   - Associate a parenthetical aside following a field with that field.
#
# Right now we do not offer any conversion between committers'
# usernames and their real names.  In the future, we could take the
# COMMITTERS file as an optional parameter and do such a transform.
#
# NOTES: You might be wondering, why not take 'svn log --xml' input?
# Well, that would be the Right Thing to do, but in practice this was
# a lot easier to whip up for straight 'svn log' output.  I'd have no
# objection to it being rewritten to take XML input.

import os
import sys
import re
import shutil
import getopt

# Pretend we have true booleans on older python versions
try:
  True
except:
  True = 1
  False = 0

# Warnings and errors start with these strings.  They are typically
# followed by a colon and a space, as in "%s: " ==> "WARNING: ".
warning_prefix = "WARNING"
error_prefix = "ERROR"

def complain(msg, fatal=False):
  """Print MSG as a warning, or if FATAL is true, print it as an error
  and exit."""
  prefix = "WARNING: "
  if fatal:
    prefix = "ERROR: "
  sys.stderr.write(prefix + msg + "\n")
  if fatal:
    sys.exit(1)


def escape_html(str):
  """Return an HTML-escaped version of STR."""
  return str.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def html_header(title):
  title = escape_html(title)
  s  = '<title>%s</title>\n' % title
  s += '<html>\n\n'
  s += '<body text="#000000" bgcolor="#FFFFFF">\n\n'
  s += '<center><h1>%s</h1></center>\n\n' % title
  s += '<hr />\n\n'
  return s


def html_footer():
  return '\n</body>\n</html>\n'


class Contributor:
  # Map contributor names to contributor instances, so that there
  # exists exactly one instance associated with a given name.
  # Fold names with email addresses.  That is, if we see someone
  # listed first with just an email address, but later with a real
  # name and that same email address together, we create only one
  # instance, and store it under both the email and the real name.
  all_contributors = { }

  # See __hash__() for why this is necessary.
  hash_value = 1

  def __init__(self, username, real_name, email):
    """Instantiate a contributor.  Don't use this to generate a
    Contributor for an external caller, though, use .get() instead."""
    self.real_name = real_name
    self.username  = username
    self.email     = email
    # Map verbs (e.g., "Patch", "Suggested", "Review") to lists of
    # LogMessage objects.  For example, the log messages stored under
    # "Patch" represent all the revisions for which this contributor
    # contributed a patch.
    self.activities = { }
    # Sigh.
    self.unique_hash_value = Contributor.hash_value
    Contributor.hash_value += 1

  def add_activity(self, field_name, log):
    """Record that this contributor was active in FIELD_NAME in LOG."""
    logs = self.activities.get(field_name)
    if not logs:
      logs = [ ]
      self.activities[field_name] = logs
    if not log in logs:
      logs.append(log)

  def get(username, real_name, email):
    """If this contributor is already registered, just return it;
    otherwise, register it then return it.  Hint: use parse() to
    generate the arguments."""
    c = None
    for key in username, real_name, email:
      if key and Contributor.all_contributors.has_key(key):
        c = Contributor.all_contributors[key]
        break
    # If we didn't get a Contributor, create one now.
    if not c:
      c = Contributor(username, real_name, email)
    # If we know identifying information that the Contributor lacks,
    # then give it to the Contributor now.
    #
    # ### FIXME: Some people are cited in different, non-intersecting
    # ### ways.  For example, Mike Pilato might appear as "cmpilato"
    # ### in one commit, and "C. Michael Pilato <cmpilato@collab.net>"
    # ### in another.  Although we can't automatically determine that
    # ### these are the same person with 100% certainty, it's pretty
    # ### obvious most of the time, and the script ought to warn, or
    # ### unify on certain criteria, or something.  At the very least,
    # ### it could put the names near each other, so a human could
    # ### catch it by eye.  (Also, if took the COMMITTERS file as
    # ### input, we probably *could* unify a lot of those pairs.)
    if username:
      if not c.username:
        c.username = username
      Contributor.all_contributors[username]  = c
    if real_name:
      if not c.real_name:
        c.real_name = real_name
      Contributor.all_contributors[real_name] = c
    if email:
      if not c.email:
        c.email = email
      Contributor.all_contributors[email]     = c
    # This Contributor has never been in better shape; return it.
    return c
  get = staticmethod(get)

  def __cmp__(self, other):
    return cmp(self.big_name(), other.big_name())

  def __hash__(self):
    """See LogMessage.__hash__() for why this exists."""
    return self.hash_value

  def parse(name):
    """Parse NAME, which can be

       - A committer username, or
       - A space-separated real name, or
       - A space-separated real name followed by an email address in
           angle brackets, or
       - Just an email address in angle brackets.

     Return a tuple of (committer_username, real_name, email_address)
     any of which can be None if not available in NAME."""
    username  = None
    real_name = None
    email     = None
    name_components = name.split()
    if len(name_components) == 1:
      if name_components[0] != name:
        complain("Python couldn't have done that, could it?", True)
      if name[0] == '<' and name[-1] == '>':
        email = name[1:-1]
      elif name.find("@") != -1:
        email = name
      else:
        username = name
    elif name_components[-1][0] == '<' and name_components[-1][-1] == '>':
      real_name = ' '.join(name_components[0:-1])
      email = name_components[-1][1:-1]
    else:
      real_name = ' '.join(name_components)
    return username, real_name, email
  parse = staticmethod(parse)

  def canonical_name(self):
    """Return a canonical name for this contributor, containing only
    characters valid in an email address.  The canonical name may or
    may not be based on the contributor's actual email address, but in
    any case it will be restricted to email-safe characters, because
    those are also likely to be filesystem-safe and HTML-safe.

    This method is guaranteed to return the same canonical name every
    time only if no further contributions are recorded from this
    contributor after the first call.  This is because a contribution
    may bring a new form of the contributor's name, one which affects
    the algorithm used to construct canonical names."""
    if self.username:
      return self.username
    if self.email:
      # Take some rudimentary steps to shorten the email address, to
      # make it more manageable.  If this is ever discovered to result
      # in collisions, we can always just use to the full address.
      at_posn = self.email.find("@")
      if not at_posn:
        return self.email
      else:
        first_dot_after_at = self.email.find(".", at_posn)
        return self.email[0:first_dot_after_at]
    if self.real_name:
      # Last resort: construct canonical name based on real name.  
      # ### FIXME: Need to tweak to guarantee that it's made only of
      # ### characters that would be safe in an email address.
      return "".join(self.real_name.lower().split(" "))
    complain("Unable to construct a canonical name for Contributor.", True)

  def big_name(self):
    """Return as complete a name as possible for this contributor."""
    s = ""
    if self.real_name: s += " " + self.real_name
    if self.email:
      if not self.real_name and not self.username:
        s += " " + self.email
      else:
        s += " <" + self.email + ">"
    if self.username:
      if not self.real_name and not self.email:
        s += " " + self.username
      else:
        s += " (" + self.username + ")"
    return s[1:]

  def __str__(self):
    s = "CONTRIBUTOR: "
    s += self.big_name()
    s += "\ncanonical name: '%s'" % self.canonical_name()
    if len(self.activities) > 0:
      s += "\n   "
    for activity in self.activities.keys():
      val = self.activities[activity]
      s += "[%s:" % activity
      for log in val:
        s += " %s" % log.revision
      s += "]"
    return s

  def html_out(self):
    """Create an HTML file in the current directory, named
    "`self.canonical_name()`.html", showing all the revisions in which
    this contributor was active."""
    canon = self.canonical_name()
    out = open(canon + ".html", "w")
    out.write(html_header(self.big_name()))
    unique_logs = { }

    sorted_activities = self.activities.keys()
    sorted_activities.sort()

    out.write('<div class="h2" id="activities" title="activities">\n\n')
    out.write('<table border="1">\n')
    out.write('<tr>\n')
    for activity in sorted_activities:
      out.write('<td>%s</td>\n\n' % activity)
    out.write("</tr>\n")
    out.write('<tr>\n')
    for activity in sorted_activities:
      out.write('<td>\n')
      first_activity = True
      for log in self.activities[activity]:
        s = ",\n"
        if first_activity:
          s = ""
          first_activity = False
        out.write('%s<a href="#%s">%s</a>' % (s, log.revision, log.revision))
        unique_logs[log] = True
      out.write('</td>\n')
    out.write('</tr>\n')
    out.write('</table>\n\n')
    out.write('</div>\n\n')

    sorted_logs = unique_logs.keys()
    sorted_logs.sort()
    for log in sorted_logs:
      out.write("<hr />\n")
      out.write('<div class="h3" id="%s" title="%s">\n' % (log.revision,
                                                           log.revision))
      out.write("<pre>\n")
      out.write("<b>%s | %s | %s</b>\n\n" % (log.revision,
                                             escape_html(log.committer),
                                             escape_html(log.date)))
      out.write(escape_html(log.message))
      out.write("</pre>\n")
      out.write('</div>\n\n')
    out.write("<hr />\n")

    out.write(html_footer())
    out.close()


class Field:
  """One field in one log message."""
  def __init__(self, name):
    # The name of this field (e.g., "Patch", "Review", etc).
    self.name = name
    # A list of contributor objects, in the order in which they were
    # encountered in the field.
    self.contributors = [ ]
    # Any parenthesized asides immediately following the field.  The
    # parentheses and trailing newline are left on.  In theory, this
    # supports concatenation of consecutive asides.  In practice, the
    # parser only detects the first one anyway, because additional
    # ones are very uncommon and furthermore by that point one should
    # probably be looking at the full log message.
    self.addendum = ""
  def add_contributor(self, contributor):
    self.contributors.append(contributor)
  def add_endum(self, addendum):
    self.addendum += addendum
  def __str__(self):
    s = "FIELD: %s (%d contributors)\n" % (self.name, len(self.contributors))
    for contributor in self.contributors:
      s += str(contributor) + "\n"
    s += self.addendum
    return s


class LogMessage:
  # Maps revision strings (e.g., "r12345") onto LogMessage instances,
  # holding all the LogMessage instances ever created.
  all_logs = { }
  def __init__(self, revision, committer, date):
    """Instantiate a log message.  All arguments are strings,
    including REVISION, which should retain its leading 'r'."""
    self.revision = revision
    self.committer = committer
    self.date = date
    self.message = ""
    # Map field names (e.g., "Patch", "Review", "Suggested") onto
    # Field objects.
    self.fields = { }
    if LogMessage.all_logs.has_key(revision):
      complain("Revision '%s' seen more than once" % revision, True)
    LogMessage.all_logs[revision] = self
  def add_field(self, field):
    self.fields[field.name] = field
  def accum(self, line):
    """Accumulate one more line of raw message."""
    self.message += line

  def __cmp__(self, other):
    """Compare two log messages by revision number, for sort().
    Return -1, 0 or 1 depending on whether a > b, a == b, or a < b.
    Note that this is reversed from normal sorting behavior, but it's
    what we want for reverse chronological ordering of revisions."""
    a = int(self.revision[1:])
    b = int(other.revision[1:])
    if a > b: return -1
    if a < b: return 1
    else:     return 0
    
  def __hash__(self):
    """I don't really understand why defining __cmp__() but not
    __hash__() renders an object type unfit to be a dictionary key,
    especially in light of the recommendation that if a class defines
    mutable objects and implements __cmp__() or __eq__(), then it
    should not implement __hash__().  See these for details:
    http://mail.python.org/pipermail/python-dev/2004-February/042580.html
    http://mail.python.org/pipermail/python-bugs-list/2003-December/021314.html

    In the meantime, I think it's safe to use the revision as a hash value."""
    return int(self.revision[1:])

  def __str__(self):
    s = "=" * 15
    header = " LOG: %s | %s " % (self.revision, self.committer)
    s += header
    s += "=" * 15
    s += "\n"
    for field_name in self.fields.keys():
      s += str(self.fields[field_name]) + "\n"
    s += "-" * 15
    s += "-" * len(header)
    s += "-" * 15
    s += "\n"
    return s



### Code to parse the logs. ##

log_separator = "-" * 72 + '\n'
log_header_re = re.compile\
                ("^(r[0-9]+) \| ([^|]+) \| ([^|]+) \| ([0-9]+)[^0-9]")
field_re = re.compile("^(Patch|Review|Suggested) by:\s+(.*)")
parenthetical_aside_re = re.compile("^\(.*\)\s*$")

def graze(input):
  just_saw_separator = False
  
  while True:
    line = input.readline()
    if line == '': break
    if line == log_separator:
      if just_saw_separator:
        sys.stderr.write("Two separators in a row.\n")
        sys.exit(1)
      else:
        just_saw_separator = True
        num_lines = None
        continue
    else:
      if just_saw_separator:
        m = log_header_re.match(line)
        if not m:
          sys.stderr.write("Could not match log message header.\n")
          sys.stderr.write("Line was:\n")
          sys.stderr.write("'%s'\n" % line)
          sys.exit(1)
        else:
          log = LogMessage(m.group(1), m.group(2), m.group(3))
          num_lines = int(m.group(4))
          just_saw_separator = False
          line = input.readline()
          # Handle 'svn log -v' by waiting for the blank line.
          while line != '\n':
            line = input.readline()
          # Parse the log message.
          field = None
          while num_lines > 0:
            line = input.readline()
            log.accum(line)
            m = field_re.match(line)
            if m:
              # We're on the first line of a field.  Parse the field.
              while m:
                if not field:
                  field = Field(m.group(1))
                # Each line begins either with "WORD by:", or with whitespace.
                in_field_re = re.compile("^("
                                         + field.name
                                         + " by:\s+|\s+)(\S.*)+")
                m = in_field_re.match(line)
                user, real, email = Contributor.parse(m.group(2))
                if user == "me":
                  user = log.committer
                c = Contributor.get(user, real, email)
                c.add_activity(field.name, log)
                field.add_contributor(c)
                line = input.readline()
                log.accum(line)
                num_lines -= 1
                m = in_field_re.match(line)
                if not m:
                  m = field_re.match(line)
                  if not m:
                    aside_match = parenthetical_aside_re.match(line)
                    if aside_match:
                      field.add_endum(line)
                  log.add_field(field)
                  field = None
            num_lines -= 1
        continue

def drop():
  # Output the data.
  #
  # The data structures are all linked up nicely to one another.  You
  # can get all the LogMessages, and each LogMessage contains all the
  # Contributors involved with that commit; likewise, each Contributor
  # points back to all the LogMessages it contributed to.
  #
  # However, the HTML output is pretty simple right now.  It's not take
  # full advantage of all that cross-linking.  For each contributor, we
  # just create a file listing all the revisions contributed to; and we
  # build a master index of all contributors, each name being a link to
  # that contributor's individual file.  Much more is possible... but
  # let's just get this up and running first.

  for key in LogMessage.all_logs.keys():
    # You could print out all log messages this way, if you wanted to.
    pass
    # print LogMessage.all_logs[key]

  index = open("index.html", "w")
  index.write(html_header("Contributors"))
  index.write("<ul>\n")
  # The same contributor appears under multiple keys, so uniquify.
  seen_contributors = { }
  # Sorting alphabetically is acceptable, but even better would be to
  # sort by number of contributions, so the most active people appear at
  # the top -- that way we know whom to look at first for commit access
  # proposals.
  sorted_contributors = Contributor.all_contributors.values()
  sorted_contributors.sort()
  for c in sorted_contributors:
    if not seen_contributors.has_key(c):
      index.write('<li><p><a href="%s.html">%s</a></p></li>\n'
                  % (c.canonical_name(), escape_html(c.big_name())))
      c.html_out()
    seen_contributors[c] = True
  index.write("</ul>\n")
  index.write(html_footer())
  index.close()


def process_committers(commiters_file):
  """Read from open file handle COMMITTERS_FILE, which should be in
  the same format as the Subversion 'COMMITTERS' file.  Create
  Contributor objects based on the contents."""
  complain("-C option not actually supported yet")


def usage():
  print 'USAGE: %s [-C COMMITTERS_FILE] < SVN_LOG_OR_LOG-V_OUTPUT' \
        % os.path.basename(sys.argv[0])
  print ""
  print "Create HTML files in the current directory, rooted at index.html,"
  print "in which you can browse to see who contributed what."
  print ""
  print "The log input should use the contribution-tracking format defined"
  print "in http://subversion.tigris.org/hacking.html#crediting."
  print ""
  print "Options:"
  print ""
  print "  -h, -H, -?, --help   Print this usage message and exit\n"
  print "  -C FILE              Use FILE as the 'COMMITTERS' file\n"
  print ""


def main():
  try:
    opts, args = getopt.getopt(sys.argv[1:], 'C:hH?', [ "--help" ])
  except getopt.GetoptError, e:
    complain(str(e) + '\n\n')
    usage()
    sys.exit(1)

  # Parse options.
  for opt, value in opts:
    if (opt == '--help') or (opt == '-h') or (opt == '-H') or (opt == '-?'):
      usage()
      sys.exit(0)
    elif opt == '-C':
      process_committers(open(value))

  # Gather the data.
  graze(sys.stdin)

  # Output the data.
  drop()

if __name__ == '__main__':
  main()
