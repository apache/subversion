#!/usr/bin/env python

# Less terrible, ugly hack of a script than getsigs.pl, but similar.  Used to
# verify the signatures on the release tarballs and produce the list of who
# signed them in the format we use for the announcements.
#
# To use just run it in the directory with the signatures and tarballs and
# pass the version of subversion you want to check.  It assumes gpg is on
# your path, if it isn't you should fix that. :D
#
# Script will die if any gpg process returns an error.
#
# Because I hate perl...

import glob, subprocess, shutil, sys, re

key_start = '-----BEGIN PGP SIGNATURE-----\n'
sig_pattern = re.compile(r'^gpg: Signature made .*? using \w+ key ID (\w+)')
fp_pattern = re.compile(r'^pub\s+(\w+\/\w+)[^\n]*\n\s+Key\sfingerprint\s=((\s+[0-9A-F]{4}){10})\nuid\s+([^<\(]+)\s')


def grab_sig_ids():
    good_sigs = {}

    for filename in glob.glob('subversion-*.asc'):
        shutil.copyfile(filename, '%s.bak' % filename)
        text = open(filename).read()
        keys = text.split(key_start)

        for key in keys[1:]:
            open(filename, 'w').write(key_start + key)
            gpg = subprocess.Popen(['gpg', '--logger-fd', '1',
                                    '--verify', filename],
                                   stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT)

            rc = gpg.wait()
            output = gpg.stdout.read()
            if rc:
                # gpg choked, die with an error
                print(output)
                sys.stderr.write("BAD SIGNATURE in %s\n" % filename)
                shutil.move('%s.bak' % filename, filename)
                sys.exit(1)

            for line in output.split('\n'):
                match = sig_pattern.match(line)
                if match:
                    key_id = match.groups()[0]
                    good_sigs[key_id] = True

        shutil.move('%s.bak' % filename, filename)

    return good_sigs


def generate_output(good_sigs):
    for id in good_sigs.keys():
        gpg = subprocess.Popen(['gpg', '--fingerprint', id],
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        rc = gpg.wait()
        gpg_output = gpg.stdout.read()
        if rc:
            print(gpg_output)
            sys.stderr.write("UNABLE TO GET FINGERPRINT FOR %s" % id)
            sys.exit(1)

        fp = fp_pattern.match(gpg_output).groups()
        print("   %s [%s] with fingerprint:" % (fp[3], fp[0]))
        print("   %s" % fp[1])


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Give me a version number!")
        sys.exit(1)

    generate_output(grab_sig_ids())
