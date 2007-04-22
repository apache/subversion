# Run this script to generate a new version of functions.py
# from the headers on your system

import os, sys, re, errno, os, tempfile
from optparse import OptionParser
from tempfile import mkdtemp
from glob import glob

parser = OptionParser()
parser.add_option("-a", "--apr-config", dest="apr_config",
                  help="The full path to your apr-1-config or apr-config script")
parser.add_option("-p", "--prefix", dest="prefix",
                  help="Specify the prefix where Subversion is installed (e.g. /usr, or /usr/local)")

(options, args) = parser.parse_args()

########################################################################
# Get the APR configuration
########################################################################

def get_apr_config():
    flags = []
    ldflags = []
    ld_library_path = []
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
            break
    else:
        print ferr
        raise Exception("Cannot find apr-1-config or apr-config. Please specify\n"
                        "the full path to your apr-1-config or apr-config script\n"
                        "using the --apr-config option.")

    subversion_prefixes = [
        prefix,
        "/usr/local",
        "/usr"
    ]

    for svn_prefix in subversion_prefixes:
        svn_include_dir = "%s/include/subversion-1" % svn_prefix
        if os.path.exists("%s/svn_client.h" % svn_include_dir):
            flags.append("-I%s" % svn_include_dir)
            break
    else:
        print ferr
        raise Exception("Cannot find svn_client.h. Please specify the prefix\n"
                        "to your Subversion installation using the --prefix\n"
                        "option.")

    ldflags = [
        "-llibapr-%s.so" % apr_version[0],
        "-llibaprutil-%s.so" % apr_version[0],
    ]

    # List the libraries in the order they should be loaded
    libraries = [ 
        "libsvn_subr-1.so",
        "libsvn_diff-1.so",
        "libsvn_delta-1.so",
        "libsvn_fs_util-1.so",
        "libsvn_fs_fs-1.so",
        "libsvn_fs_base-1.so",
        "libsvn_fs-1.so",
        "libsvn_wc-1.so",
        "libsvn_ra_local-1.so",
        "libsvn_ra_svn-1.so",
        "libsvn_ra_dav-1.so",
        "libsvn_ra_serf-1.so",
        "libsvn_ra-1.so",
        "libsvn_client-1.so",
    ]

    for lib in libraries:
        if os.path.exists("%s/lib/%s" % (svn_prefix, lib)):
            ldflags.append("-l%s" % lib)

    return (svn_prefix, apr_prefix, apr_include_dir, cpp,
            " ".join(ldflags), " ".join(flags), ":".join(ld_library_path))

def run_cmd(cmd):
    return os.popen(cmd).read()

(svn_prefix, apr_prefix, apr_include_dir, cpp, ldflags, flags,
 ld_library_path) = get_apr_config()

########################################################################
# Build csvn/core/functions.py
########################################################################

tempdir = mkdtemp()

includes = ('%s/include/subversion-1/svn_*.h '
            '%s/include/apr-1/ap[ru]_*.h' % (apr_prefix, svn_prefix))

os.environ["LD_LIBRARY_PATH"] = ld_library_path
os.system("cd %s && python %s/ctypesgen/wrap.py %s %s "
          "%s -o svn_all.py" % (tempdir, os.getcwd(),
                                flags, ldflags, includes))

func_re = re.compile(r"CFUNCTYPE\(POINTER\((\w+)\)")
out = file("%s/svn_all2.py" % tempdir, "w")
for line in file("%s/svn_all.py" % tempdir):
    line = line.replace("restype = POINTER(svn_error_t)",
                        "restype = SVN_ERR")
    line = line.replace("'%s/lib/" % svn_prefix, "'")
    line = line.replace("'%s/lib/" % apr_prefix, "'")
    if not line.startswith("FILE ="):
        out.write(line)
out.close()
os.system("cat csvn/core/functions.py.in %s/svn_all2.py "
          "> csvn/core/functions.py" % tempdir)

os.system("rm -rf %s" % tempdir)

# Output a nice message explaining that everything was generated,
# hopefully OK :)
print "Generated csvn/core/functions.py OK"
