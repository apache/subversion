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
# A script intended to be useful in helping to collect signatures for a
# release.  This is a pretty rough, and patches are welcome to improve it.
#
# Some thoughts about future improvement:
#  * Display of per-file and per-release statistics
#  * Make use of the python-gpg package (http://code.google.com/p/python-gnupg/)
#  * Post to IRC when a new signature is collected
#    - Since we don't want to have a long running bot, perhaps we could
#      also patch wayita to accept and then echo a privmsg?
#  * Mail dev@ when somebody submits a successful signature, and include a
#    comments field which could be included in the mail.
#  * Use a subversion repository instead of sqlite backend
#    - no need to re-invent storage and retrieval
#    - perhaps we could re-use existing CIA/mailer hooks?
#

import sys, os
import sqlite3

def make_config():
  'Output a blank config file'

  if os.path.exists('config.py'):
    print "'config.py' already exists!'"
    sys.exit(1)

  conf = open('config.py', 'w')
  conf.write("version = ''\n")
  conf.write("sigdir = ''\n")
  conf.write("filesdir = ''\n")
  conf.close()

  print "'config.py' generated"

def make_db():
  'Initialize a blank database'

  db = sqlite3.connect('sigs.db')
  db.execute('''
    CREATE TABLE signatures (
      keyid TEXT, filename TEXT, signature BLOB,
      UNIQUE(keyid,filename)
    );
''');

# This function is web-facing
def generate_asc_files(target_dir='.'):
  fds = {}
  def _open(filename):
    if not fds.has_key(filename):
      fd = open(os.path.join(target_dir, filename + '.asc'), 'w')
      fds[filename] = fd
    return fds[filename]

  db = sqlite3.connect(os.path.join(target_dir, 'sigs.db'))
  curs = db.cursor()
  like_filename = 'subversion-%s.%%' % config.version
  curs.execute('''SELECT filename, signature FROM signatures
                  WHERE filename LIKE ?''', (like_filename, ) )
  for filename, signature in curs:
    fd = _open(filename)
    fd.write(signature)

  for fd in fds.values():
    fd.flush()
    fd.close()

actions = {
    'make_config' : make_config,
    'make_db' : make_db,
    'make_asc' : generate_asc_files,
}


if __name__ == '__main__':
  if len(sys.argv) > 1:
    if sys.argv[1] in actions:
      actions[sys.argv[1]]()
      sys.exit(0)


# Stuff below this line is the web-facing side
# ======================================================================


import cgi
import cgitb
cgitb.enable()

import string, subprocess, re

try:
  sys.path.append(os.path.dirname(sys.argv[0]))
  import config
except:
  print 'Content-type: text/plain'
  print
  print 'Cannot find config file'
  sys.exit(1)

r = re.compile('^\[GNUPG\:\] GOODSIG (\w*) (.*)')

def files():
  for f in os.listdir(config.filesdir):
    if config.version in f and (f.endswith('.tar.gz') or f.endswith('.zip') or f.endswith('.tar.bz2')):
      yield f

def ordinal(N):
  try:
    return [None, 'first', 'second', 'third', 'fourth', 'fifth', 'sixth'][N]
  except:
    # Huh?  We only have six files to sign.
    return "%dth" % N

shell_content = '''
<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Strict//EN"
"http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd">
<html>
<head>
<title>Signature collection for Subversion $version</title>
</head>
<body style="font-size: 14pt; text-align: justify;
  background-color: #f0f0f0; padding: 0 5%%">
<p>This page is used to collect <a href="%s/list">signatures</a> for the
proposed release of Apache Subversion $version.</p>
$content
</body>
</html>
''' % os.getenv('SCRIPT_NAME')

signature_area = '''
<hr/>
<form method="post" action="%s">
<p>Paste one or more signatures in the area below:<br/>
<textarea name="signatures" rows="20" cols="80"></textarea>
</p>
<input type="submit" value="Submit" />
<p>Any text not between the <tt>BEGIN PGP SIGNATURE</tt>
and <tt>END PGP SIGNATURE</tt> lines will be ignored.</p>
</form>
<hr/>
''' % os.getenv('SCRIPT_NAME')



def split(sigs):
  lines = []
  for line in sigs.split('\n'):
    if lines or '--BEGIN' in line:
      lines.append(line)
    if '--END' in line:
      yield "\n".join(lines) + "\n"
      lines = []

def list_signatures():
  db = sqlite3.connect(os.path.join(config.sigdir, 'sigs.db'))
  template = '''
<hr/>
<p>The following signature files are available:</p>
<p>%s</p>
'''

  lines = ""
  curs = db.cursor()
  like_filename = 'subversion-%s.%%' % config.version
  curs.execute('''SELECT filename, COUNT(*) FROM signatures
                  WHERE filename LIKE ?
                  GROUP BY filename ORDER BY filename''',
               (like_filename, ) )
  for filename, count in curs:
    lines += '<a href="%s/%s.asc">%s.asc</a>: %d signature%s<br/>\n' \
             % (os.getenv('SCRIPT_NAME'), filename, filename,
                count, ['s', ''][count == 1])
  return (template % lines) + signature_area

def save_valid_sig(db, filename, keyid, signature):
  db.execute('INSERT OR REPLACE INTO signatures VALUES (?,?,?);',
             (keyid, filename, buffer(signature)))
  db.commit()

  generate_asc_files(config.sigdir)

def verify_sig_for_file(signature, filename):
  args = ['gpg', '--logger-fd', '1', '--no-tty',
          '--status-fd', '2', '--verify', '-',
          os.path.join(config.filesdir, filename)]

  gpg = subprocess.Popen(args,
                         stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE)

  gpg.stdin.write(signature)
  gpg.stdin.close()

  rc = gpg.wait()
  output = gpg.stdout.read()
  status = gpg.stderr.read()

  if rc:
    return (False, status + output)

  lines = status.split('\n')
  for line in lines:
    match = r.search(line)
    if match:
      keyid = match.group(1)
      user = match.group(2)

  return (True, (filename, keyid, user))

def verify_sig(signature):
  all_failures = ""
  for filename in files():
    (verified, result) = verify_sig_for_file(signature, filename)
    if verified:
      return (verified, result)
    else:
      all_failures += "%s:\n[[[\n%s]]]\n\n" % (filename, result)
  return (False, all_failures)

def process_sigs(signatures):
  success = '''
  <p style="color: green;">All %d signatures verified!</p>
'''
  failure = '''
  <p style="color: red;">%d of %d signatures failed to verify; details below.</p>
'''
  c_verified = '''
  <p style="color: green;">The signature is verified!</p>
  <p>Filename: <code>%s</code></p>
  <p>Key ID: <code>%s</code></p>
  <p>User: <code>%s</code></p>
  <p>This signature has been saved, and will be included as part of the
    release signatures.</p>
'''
  c_unverified = '''
  <p style="color: red;">The signature was not able to be verified!</p>
  <p>Signature: <pre>%s</pre></p>
  <p>Reason:</p><pre>%s</pre>
  <p>Please talk to the release manager if this is in error.</p>
'''

  outcomes = []
  N_sigs = 0
  N_verified = 0
  retval = ''

  # Verify
  db = sqlite3.connect(os.path.join(config.sigdir, 'sigs.db'))
  for signature in split(signatures):
    N_sigs += 1
    (verified, result) = verify_sig(signature)
    outcomes.append((verified, result))

    if verified:
      (filename, keyid, user) = result
      save_valid_sig(db, filename, keyid, signature)
      N_verified += 1

  # Output header
  if N_verified == N_sigs:
    retval += success % N_sigs
  else:
    retval += failure % (N_sigs-N_verified, N_sigs)

  # Output details
  N = 0
  for outcome in outcomes:
    N += 1
    (verified, result) = outcome
    retval += "<h1>Results for the %s signature</h1>" % ordinal(N)
    if verified:
      (filename, keyid, user) = result
      retval += c_verified % (filename, keyid[-8:], user)
    else:
      retval += c_unverified % (signature, result)

  return retval + signature_area


def cat_signatures(basename):
  # strip '.asc' extension
  assert basename[:-4] in files()

  # cat
  ascfile = os.path.join(config.sigdir, basename)
  if os.path.exists(ascfile):
    return (open(ascfile, 'r').read())

def print_content_type(mimetype):
  print "Content-Type: " + mimetype
  print

def main():
  form = cgi.FieldStorage()
  pathinfo = os.getenv('PATH_INFO')

  # default value, to be changed below
  content = signature_area

  if 'signatures' in form:
    content = process_sigs(form['signatures'].value)

  elif pathinfo and pathinfo[1:]:
    basename = pathinfo.split('/')[-1]

    if basename == 'list':
      content = list_signatures()

    elif basename[:-4] in files():
      # early exit; bypass 'content' entirely
      print_content_type('text/plain')
      print cat_signatures(basename)
      return

  # These are "global" values, not specific to our action.
  mapping = {
      'version' : config.version,
      'content' : content,
    }

  print_content_type('text/html')

  template = string.Template(shell_content)
  print template.safe_substitute(mapping)


if __name__ == '__main__':
  main()
