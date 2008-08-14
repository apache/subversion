#!/usr/bin/env python

# Run this script to generate a new version of functions.py
# from the headers on your system

import os, sys, re, errno, os, tempfile
from optparse import OptionParser
from tempfile import mkdtemp
from glob import glob

def option_W(option, opt, value, parser):
    if len(value) < 4 or value[0:3] != 'l,-':
        raise optparse.BadOptionError("not in '-Wl,<opt>' form: %s%s"
                                      % (opt, value))
    opt = value[2:]
    if opt not in ['-L', '-R', '--rpath']:
        raise optparse.BadOptionError("-Wl option must be -L, -R"
                                      " or --rpath, not " + value[2:])
    # Push the linker option onto the list for further parsing.
    parser.rargs.insert(0, value)

parser = OptionParser()
parser.add_option("-a", "--apr-config", dest="apr_config",
                  help="The full path to your apr-1-config or apr-config script")
parser.add_option("-u", "--apu-config", dest="apu_config",
                  help="The full path to your apu-1-config or apu-config script")
parser.add_option("-p", "--prefix", dest="prefix",
                  help="Specify the prefix where Subversion is installed (e.g. /usr, or /usr/local)")

parser.add_option("", "--save-preprocessed-headers", dest="filename",
                  help="Save the preprocessed headers to the specified "
                       "FILENAME")
parser.add_option("-W", action="callback", callback=option_W, type='str',
                  metavar="l,OPTION",
                  help="where OPTION is -L, -R, or --rpath")
parser.add_option("-L", "-R", "--rpath", action="append", dest="libdirs",
                  metavar="LIBDIR", help="Add LIBDIR to the search path")
parser.set_defaults(libdirs=[])

(options, args) = parser.parse_args()

########################################################################
# Get the APR configuration
########################################################################

def get_apr_config():
    flags = []
    ldflags = []
    library_path = []
    ferr = None
    prefix = options.prefix
    apr_include_dir = None
    apr_config_paths = ('apr-1-config', 'apr-config')
    if options.apr_config:
        apr_config_paths = (options.apr_config,)
    elif prefix:
        apr_config_paths = ('%s/bin/apr-1-config' % prefix,
                            '%s/bin/apr-config' % prefix,
                            'apr-1-config', 'apr-config')

    for apr_config in apr_config_paths:
        fout = run_cmd("%s --includes --cppflags" % apr_config)
        if fout:
            flags = fout.split()
            apr_prefix = run_cmd("%s --prefix" % apr_config)
            apr_prefix = apr_prefix.strip()
            if not prefix:
                prefix = apr_prefix
            apr_include_dir = run_cmd("%s --includedir" % apr_config).strip()
            apr_version = run_cmd("%s --version" % apr_config).strip()
            cpp  = run_cmd("%s --cpp" % apr_config).strip()

            fout = run_cmd("%s --ldflags --link-ld" % apr_config)
            if fout:
                ldflags = fout.split()
            break
    else:
        print ferr
        raise Exception("Cannot find apr-1-config or apr-config. Please specify\n"
                        "the full path to your apr-1-config or apr-config script\n"
                        "using the --apr-config option.")

    if options.apu_config:
        apu_config_paths = (options.apu_config,)
    elif apr_version[0] == "1":
        apu_config_paths = ('%s/bin/apu-1-config' % apr_prefix,
                            '%s/bin/apu-1-config' % prefix,
                            'apu-1-config')
    else:
        apu_config_paths = ('%s/bin/apu-config' % apr_prefix,
                            '%s/bin/apu-config' % prefix,
                            'apu-config')

    for apu_config in apu_config_paths:
        fout = run_cmd("%s --includes" % apu_config)
        if fout:
            flags += fout.split()
            fout = run_cmd("%s --ldflags --link-ld" % apu_config)
            if fout:
                ldflags += fout.split()
            break
    else:
        print ferr
        raise Exception("Cannot find apu-1-config or apu-config. Please specify\n"
                        "the full path to your apu-1-config or apu-config script\n"
                        "using the --apu-config option.")


    subversion_prefixes = [
        prefix,
        "/usr/local",
        "/usr"
    ]

    for svn_prefix in subversion_prefixes:
        svn_include_dir = "%s/include/subversion-1" % svn_prefix
        if os.path.exists("%s/svn_client.h" % svn_include_dir):
            if svn_prefix != "/usr":
                ldflags.append("-L%s/lib" % svn_prefix)
            flags.append("-I%s" % svn_include_dir)
            break
    else:
        print ferr
        raise Exception("Cannot find svn_client.h. Please specify the prefix\n"
                        "to your Subversion installation using the --prefix\n"
                        "option.")

    # List the libraries in the order they should be loaded
    libraries = [ 
        "svn_subr-1",
        "svn_diff-1",
        "svn_delta-1",
        "svn_fs-1",
        "svn_repos-1",
        "svn_wc-1",
        "svn_ra-1",
        "svn_client-1",
    ]

    for lib in libraries:
        if glob("%s/lib/*%s*" % (svn_prefix, lib)):
            ldflags.append("-l%s" % lib)

    if apr_prefix != '/usr':
        library_path.append("%s/lib" % apr_prefix)
    if svn_prefix != '/usr' and svn_prefix != apr_prefix:
        library_path.append("%s/lib" % svn_prefix)

    return (svn_prefix, apr_prefix, apr_include_dir, cpp,
            " ".join(ldflags), " ".join(flags), ":".join(library_path))

def run_cmd(cmd):
    return os.popen(cmd).read()

(svn_prefix, apr_prefix, apr_include_dir, cpp, ldflags, flags,
 library_path) = get_apr_config()

########################################################################
# Build csvn/core/functions.py
########################################################################

ctypesgen_basename = "ctypesgen.py"
ctypesgen = ""
if os.path.exists(os.path.join("ctypesgen", ctypesgen_basename)):
    ctypesgen = os.path.join(os.getcwd(), "ctypesgen", ctypesgen_basename)
else:
    for path in os.environ["PATH"].split(os.pathsep):
        if os.path.exists(os.path.join(path, ctypesgen_basename)):
            ctypesgen = os.path.join(path, ctypesgen_basename)
            break
if ctypesgen == "":
    raise Exception(
        "Cannot find ctypesgen. Please download the ctypesgen package by\n"
        "following the instructions provided in README.")

tempdir = mkdtemp()

includes = ('%s/include/subversion-1/svn_*.h '
            '%s/ap[ru]_*.h' % (svn_prefix, apr_include_dir))

cmd = ["cd %s && %s %s --cpp '%s %s' %s "
       "%s -o svn_all.py" % (tempdir, sys.executable, ctypesgen,
                             cpp, flags, ldflags, includes)]
cmd.extend('-R ' + x for x in options.libdirs)
cmd = ' '.join(cmd)

if options.filename:
    cmd += " --save-preprocessed-headers=%s" % \
        os.path.abspath(options.filename)

print cmd
status = os.system(cmd)

if os.name == "posix":
    if os.WIFEXITED(status):
        status = os.WEXITSTATUS(status)
        if status != 0:
            sys.exit(status)
    elif os.WIFSIGNALED(status):
        print >>sys.stderr, "ctypesgen.py killed with signal %d" % os.WTERMSIG(status)
        sys.exit(2)
    elif os.WIFSTOPPED(status):
        print >>sys.stderr, "ctypesgen.py stopped with signal %d" % os.WSTOPSIG(status)
        sys.exit(2)
    else:
        print >>sys.stderr, "ctypesgen.py exited with invalid status %d" % status
        sys.exit(2)

func_re = re.compile(r"CFUNCTYPE\(POINTER\((\w+)\)")
out = file("%s/svn_all2.py" % tempdir, "w")
for line in file("%s/svn_all.py" % tempdir):
    line = line.replace("restype = POINTER(svn_error_t)",
                        "restype = SVN_ERR")
    if not line.startswith("FILE ="):
        out.write(line)
out.close()
os.system("cat csvn/core/functions.py.in %s/svn_all2.py "
          "> csvn/core/functions.py" % tempdir)

os.system("rm -rf %s" % tempdir)

# Output a nice message explaining that everything was generated,
# hopefully OK :)
print "Generated csvn/core/functions.py OK"
