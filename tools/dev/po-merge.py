#!/usr/bin/env python

import os, sys

def parse_translation(f):
    """Read a single translation entry from the file F and return a
    tuple with the comments, msgid and msgstr.  The comments is returned
    as a list of lines which do not end in new-lines.  The msgid and
    msgstr are strings, possibly with embedded newlines"""
    line = f.readline()

    # Parse comments
    comments = []
    while 1:
        if line.strip() == '':
            return comments, None, None
        elif line[0] == '#':
            comments.append(line[:-1])
        else:
            break
        line = f.readline()

    # Parse msgid
    if line[:7] != 'msgid "' or line[-2] != '"':
        raise RuntimeError("parse error")
    msgid = line[6:-1]
    while 1:
        line = f.readline()
        if line[0] != '"':
            break
        msgid += '\n' + line[:-1]

    # Parse msgstr
    if line[:8] != 'msgstr "' or line[-2] != '"':
        raise RuntimeError("parse error")
    msgstr = line[7:-1]
    while 1:
        line = f.readline()
        if len(line) == 0 or line[0] != '"':
            break
        msgstr += '\n' + line[:-1]

    if line.strip() != '':
        raise RuntimeError("parse error")

    return comments, msgid, msgstr

def split_comments(comments):
    """Split COMMENTS into flag comments and other comments.  Flag
    comments are those that begin with '#,', e.g. '#,fuzzy'."""
    flags = []
    other = []
    for c in comments:
        if len(c) > 1 and c[1] == ',':
            flags.append(c)
        else:
            other.append(c)
    return flags, other

def main(argv):
    if len(argv) != 2:
        argv0 = os.path.basename(argv[0])
        sys.exit('Usage: %s <lang.po>\n'
                 '\n'
                 'This script will replace the translations and flags in lang.po with\n'
                 'with the translations and flags in the source po file read from standard\n'
                 'input.  Strings that are not found in the source file are left untouched.\n'
                 'A backup copy of lang.po is saved as lang.po.bak.\n'
                 '\n'
                 'Example:\n'
                 '    svn cat http://svn.collab.net/repos/svn/trunk/subversion/po/sv.po | \\\n'
                 '        %s sv.po' % (argv0, argv0))

    # Read the source po file into a hash
    source = {}
    while 1:
        comments, msgid, msgstr = parse_translation(sys.stdin)
        if not comments and msgid is None:
            break
        if msgid is not None:
            source[msgid] = msgstr, split_comments(comments)[0]

    # Make a backup of the output file, open the copy for reading
    # and the original for writing.
    os.rename(argv[1], argv[1] + '.bak')
    infile = open(argv[1] + '.bak')
    outfile = open(argv[1], 'w')

    # Loop thought the original and replace stuff as we go
    first = 1
    string_count = 0
    update_count = 0
    untranslated = 0
    while 1:
        comments, msgid, msgstr = parse_translation(infile)
        if not comments and msgid is None:
            break
        if not first:
            outfile.write('\n')
        first = 0
        if msgid is None:
            outfile.write('\n'.join(comments) + '\n')
        else:
            string_count += 1
            # Do not update the header, and only update if the source
            # has a non-empty translation.
            if msgid != '""' and source.get(msgid, ['""', []])[0] != '""':
                other = split_comments(comments)[1]
                new_msgstr, new_flags = source[msgid]
                new_comments = other + new_flags
                if new_msgstr != msgstr or new_comments != comments:
                    update_count += 1
                    msgstr = new_msgstr
                    comments = new_comments
            outfile.write('\n'.join(comments) + '\n')
            outfile.write('msgid ' + msgid + '\n')
            outfile.write('msgstr ' + msgstr + '\n')
        if msgstr == '""':
            untranslated += 1

    # We're done.  Tell the user what we did.
    print('%d strings updated. '
          '%d of %d strings are still untranslated (%.0f%%).' %
          (update_count, untranslated, string_count,
           100.0 * untranslated / string_count))

if __name__ == '__main__':
    main(sys.argv)
