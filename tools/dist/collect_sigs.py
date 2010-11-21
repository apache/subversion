#!/usr/bin/env python

import cgi
import cgitb
cgitb.enable()

import sys, os, string, subprocess, re

try:
  sys.path.append(os.path.dirname(sys.argv[0]))
  import config
except:
  print 'Content-type: text/plain'
  print
  print 'Cannot find config file'
  sys.exit(1)

r = re.compile('\[GNUPG\:\] GOODSIG (\w*) (.*)')

shell_content = '''
<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Strict//EN"
"http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd">
<html>
<head>
<title>Signature collection for Subversion $version</title>
</head>
<body style="font-size: 14pt; text-align: justify;
  background-color: #f0f0f0; padding: 0 5%">
<p>This page is used to collect signatures for the proposed release of
Apache Subversion $version.</p>
$content
</body>
</html>
'''

def default_page():
  c = '''
<form method="post">
File: <select name="filename">
%s
</select>
<br/>
<p>Paste signature in the area below:<br/>
<textarea name="signature" rows="10" cols="80"></textarea>
</p>
<input type="submit" value="Submit" />
</form>
'''

  contents = [f for f in os.listdir('.')
              if f.endswith('.tar.gz') or f.endswith('.zip')
                                       or f.endswith('.tar.bz2')]
  contents.sort()

  options = ''
  for f in contents:
    options = options + '<option value="%s">%s</option>\n' % (f, f)

  return c % options


def save_valid_sig(filename, signature):
  f = open(os.path.join(config.sigdir, filename + '.asc'), 'a')
  f.write(signature)


def verify_sig(signature, filename):
  args = ['gpg', '--logger-fd', '1', '--no-tty',
          '--status-fd', '2', '--verify', '-', filename]

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
      keyid = match.group(1)[-8:]
      user = match.group(2)

  return (True, (keyid, user))


def process_sig(signature, filename):
  c_verified = '''
  <p style="color: green;">The signature is verified!</p>
  <p>Filename: <code>%s</code></p>
  <p>Key ID: <code>%s</code></p>
  <p>User: <code>%s</code></p>
  <p>This signature has been saved, and will be included as part of the
    release signatures.  Please send mail to
    <a href="mailto:dev@subversion.apache.org">dev@subversion.apache.org</a>
    acknowledging your successful signature.</p>
'''
  c_unverified = '''
  <p style="color: red;">The signature was not able to be verified!</p>
  <p>Filename: <code>%s</code></p>
  <p>Reason:</p><pre>%s</pre>
  <p>Please talk to the release manager if this is in error.</p>
'''

  (verified, result) = verify_sig(signature, filename)

  if verified:
    save_valid_sig(filename, signature)

    return c_verified % (filename, result[0], result[1])
  else:
    return c_unverified % (filename, result)


def main():
  print "Content-Type: text/html"
  print

  form = cgi.FieldStorage()
  if 'signature' not in form:
    content = default_page()
  else:
    content = process_sig(form['signature'].value, form['filename'].value)

  # These are "global" values, not specific to our action.
  mapping = {
      'version' : config.version,
      'content' : content,
    }

  template = string.Template(shell_content)
  print template.safe_substitute(mapping)


if __name__ == '__main__':
  main()
