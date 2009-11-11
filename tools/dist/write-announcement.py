#!/usr/bin/python

# Creates release announcement text using the contents of:
#   md5sums
#   sha1sums
#   getsigs-output (generate with getsigs.pl)
# all of which must be in the current directory.
#
# Writes output to:
#   announcement.html
#   announcement.txt

ann_text = """\
I'm happy to announce Subversion @VERSION@, available from:

    http://subversion.tigris.org/downloads/subversion-@VERSION@.tar.bz2
    http://subversion.tigris.org/downloads/subversion-@VERSION@.tar.gz
    http://subversion.tigris.org/downloads/subversion-@VERSION@.zip
    http://subversion.tigris.org/downloads/subversion-deps-@VERSION@.tar.bz2
    http://subversion.tigris.org/downloads/subversion-deps-@VERSION@.tar.gz
    http://subversion.tigris.org/downloads/subversion-deps-@VERSION@.zip

The MD5 checksums are:

@MD5SUMS@

The SHA1 checksums are:

@SHA1SUMS@

PGP Signatures are available at:

    http://subversion.tigris.org/downloads/subversion-@VERSION@.tar.bz2.asc
    http://subversion.tigris.org/downloads/subversion-@VERSION@.tar.gz.asc
    http://subversion.tigris.org/downloads/subversion-@VERSION@.zip.asc
    http://subversion.tigris.org/downloads/subversion-deps-@VERSION@.tar.bz2.asc
    http://subversion.tigris.org/downloads/subversion-deps-@VERSION@.tar.gz.asc
    http://subversion.tigris.org/downloads/subversion-deps-@VERSION@.zip.asc

For this release, the following people have provided PGP signatures:

@SIGINFO@
@RCWARNING@
Release notes for the @MAJOR_MINOR@.x release series may be found at:

    http://subversion.tigris.org/svn_@MAJOR_MINOR@_releasenotes.html

You can find the list of changes between @VERSION@ and earlier versions at:

    http://svn.collab.net/repos/svn/tags/@VERSION@/CHANGES

Questions, comments, and bug reports to users@subversion.tigris.org.

Thanks,
- The Subversion Team
"""

ann_html = """\
<p>I'm happy to announce Subversion @VERSION@, available from:</p>

<dl>
<dd><a href="http://subversion.tigris.org/downloads/subversion-@VERSION@.tar.bz2">http://subversion.tigris.org/downloads/subversion-@VERSION@.tar.bz2</a></dd>
<dd><a href="http://subversion.tigris.org/downloads/subversion-@VERSION@.tar.gz">http://subversion.tigris.org/downloads/subversion-@VERSION@.tar.gz</a></dd>
<dd><a href="http://subversion.tigris.org/downloads/subversion-@VERSION@.zip">http://subversion.tigris.org/downloads/subversion-@VERSION@.zip</a></dd>
<dd><a href="http://subversion.tigris.org/downloads/subversion-deps-@VERSION@.tar.bz2">http://subversion.tigris.org/downloads/subversion-deps-@VERSION@.tar.bz2</a></dd>
<dd><a href="http://subversion.tigris.org/downloads/subversion-deps-@VERSION@.tar.gz">http://subversion.tigris.org/downloads/subversion-deps-@VERSION@.tar.gz</a></dd>
<dd><a href="http://subversion.tigris.org/downloads/subversion-deps-@VERSION@.zip">http://subversion.tigris.org/downloads/subversion-deps-@VERSION@.zip</a></dd>
</dl>

<p>The MD5 checksums are:</p>
<dl>
@MD5SUMS@
</dl>

<p>The SHA1 checksums are:</p>

<dl>
@SHA1SUMS@
</dl>

<p>PGP Signatures are available at:</p>

<dl>
<dd><a href="http://subversion.tigris.org/downloads/subversion-@VERSION@.tar.bz2.asc">http://subversion.tigris.org/downloads/subversion-@VERSION@.tar.bz2.asc</a></dd>
<dd><a href="http://subversion.tigris.org/downloads/subversion-@VERSION@.tar.gz.asc">http://subversion.tigris.org/downloads/subversion-@VERSION@.tar.gz.asc</a></dd>
<dd><a href="http://subversion.tigris.org/downloads/subversion-@VERSION@.zip.asc">http://subversion.tigris.org/downloads/subversion-@VERSION@.zip.asc</a></dd>
<dd><a href="http://subversion.tigris.org/downloads/subversion-deps-@VERSION@.tar.bz2.asc">http://subversion.tigris.org/downloads/subversion-deps-@VERSION@.tar.bz2.asc</a></dd>
<dd><a href="http://subversion.tigris.org/downloads/subversion-deps-@VERSION@.tar.gz.asc">http://subversion.tigris.org/downloads/subversion-deps-@VERSION@.tar.gz.asc</a></dd>
<dd><a href="http://subversion.tigris.org/downloads/subversion-deps-@VERSION@.zip.asc">http://subversion.tigris.org/downloads/subversion-deps-@VERSION@.zip.asc</a></dd>
</dl>

<p>For this release, the following people have provided PGP signatures:</p>

<dl>
@SIGINFO@
</dl>
@RCWARNING@
<p>Release notes for the @MAJOR_MINOR@.x release series may be found at:</p>

<dl><dd><a href="http://subversion.tigris.org/svn_@MAJOR_MINOR@_releasenotes.html">http://subversion.tigris.org/svn_@MAJOR_MINOR@_releasenotes.html</a></dd></dl>

<p>You can find the list of changes between @VERSION@ and earlier versions at:</p>

<dl><dd><a href="http://svn.collab.net/repos/svn/tags/@VERSION@/CHANGES">http://svn.collab.net/repos/svn/tags/@VERSION@/CHANGES</a></dd></dl>
"""

rc_warning_text = [ """\

The term 'release candidate' means the Subversion developers feel that this
release is stable and ready to be tested in production use.  If this testing
confirms its readiness, this candidate version will become the final released
version.  Therefore, we encourage people to test this release thoroughly.
""",
"""\
As a note to operating system distro packagers: while we wish to have this
release candidate widely tested, we do not feel that it is ready for packaging
and providing to end-users through a distro package system.  Packaging a
release candidate poses many problems, the biggest being that our policy lets
us break compatibility between the release candidate and the final release, if
we find something serious enough.  Having many users depending on a release
candidate through their distro would cause no end of pain and frustration that
we do not want to have to deal with.  However, if your distro has a branch that
is clearly labeled as containing experimental and often broken software, and
explicitly destined to consenting developers and integrators only, then we're
okay with packaging the release candidate there.  Just don't let it near the
end users please.
""" ]

import sys, re

def fmtsums_text(sumlist):
    return "\n".join(["    " + x for x in sumlist])

def fmtsums_html(sumlist):
    return "\n".join(["<dd>" + x + "</dd>" for x in sumlist])

def main():
    global ann_text
    global ann_html
    version = sys.argv[1]
    match = re.match(r'^\d+\.\d+\.\d+(-(alpha|beta|rc)\d+)?$', version)
    if not match:
        print("Did you really mean to use version '%s'?" % version)
        return

    if match.group(1):
      warning_text = rc_warning_text
    else:
      warning_text = ''

    md5sums = []
    sha1sums = []
    siginfo = []
    for line in open('md5sums'):
        if line.find('subversion-') == -1:
            continue
        md5sums.append(line.strip('\n'))
    for line in open('sha1sums'):
        if line.find('subversion-') == -1:
            continue
        sha1sums.append(line.strip('\n'))
    for line in open('getsigs-output'):
        siginfo.append(line.rstrip('\n'))

    ann_text = ann_text.replace('@VERSION@', version)
    ann_html = ann_html.replace('@VERSION@', version)
    ann_text = ann_text.replace('@MAJOR_MINOR@', version[0:3])
    ann_html = ann_html.replace('@MAJOR_MINOR@', version[0:3])
    ann_text = ann_text.replace('@MD5SUMS@', fmtsums_text(md5sums))
    ann_text = ann_text.replace('@SHA1SUMS@', fmtsums_text(sha1sums))
    ann_html = ann_html.replace('@MD5SUMS@', fmtsums_html(md5sums))
    ann_html = ann_html.replace('@SHA1SUMS@', fmtsums_html(sha1sums))
    ann_text = ann_text.replace('@RCWARNING@', '\n'.join(warning_text))
    ann_html = ann_html.replace('@RCWARNING@',
                       '\n'.join([ '<p>' + x + '</p>' for x in warning_text]))

    ann_text = ann_text.replace('@SIGINFO@', "\n".join(siginfo))
    htmlsigs = []
    for i in range(0, len(siginfo), 2):
        htmlsigs.append("<dd>" + siginfo[i].strip() + "\n" +
                siginfo[i+1].strip() + "</dd>")
    ann_html = ann_html.replace('@SIGINFO@', "\n".join(htmlsigs))

    open('announcement.txt', 'w').write(ann_text)
    open('announcement.html', 'w').write(ann_html)

if __name__ == '__main__':
    main()
