#!/usr/bin/env python
#
#  autoprop_tests.py:  testing automatic properties
#
#  Subversion is a tool for revision control. 
#  See http://subversion.tigris.org for more information.
#    
# ====================================================================
# Copyright (c) 2000-2003 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
######################################################################

# General modules
import string, sys, re, os, os.path, shutil

# Our testing module
import svntest


# (abbreviation)
Skip = svntest.testcase.Skip
XFail = svntest.testcase.XFail
Item = svntest.wc.StateItem


# Helper functions
def check_prop(name, path, exp_out):
  """Verify that property NAME on PATH has a value of EXP_OUT"""
  # Not using run_svn because binary_mode must be set
  out, err = svntest.main.run_svn(None, 'pg', '--strict', name, path)
  if out != exp_out:
    print "Expected standard output: ", exp_out, "\n"
    print "Actual standard output: ", out, "\n"
    raise svntest.Failure


def check_proplist(path, exp_out):
  """Verify that property list on PATH has a value of EXP_OUT"""
  # Not using run_svn because binary_mode must be set
  out, err = svntest.main.run_svn(None, 'proplist', path)
  if len(out) == 0 and len(exp_out) == 0:
    # no properties expected and svn didn't output anything so it's ok
    return

  if len(out) < 1:
    print "Expected result: ", exp_out, "\n"
    print "Actual standard output: ", out, "\n"
    raise svntest.Failure
  out2 = []
  if len(out) > 1:
    for line in out[1:]:
      out2 = out2 + [string.strip(line)]
  out2.sort()
  exp_out.sort()
  if out2 != exp_out:
    print "Expected result: ", exp_out, "\n"
    print "Actual result: ", out2, "\n"
    print "Actual standard output: ", out, "\n"
    raise svntest.Failure


######################################################################
# Tests

#----------------------------------------------------------------------

def create_config(config_dir, enable_flag):
  "create config directories and files"

  # config file names
  cfgfile_cfg = os.path.join(config_dir, 'config')
  cfgfile_srv = os.path.join(config_dir, 'server')

  # create the directory
  if not os.path.isdir(config_dir):
    os.makedirs(config_dir)

  # create the file 'config'
  fd = open(cfgfile_cfg, 'w')
  fd.write('[miscellany]\n')
  fd.write('enable-auto-props = ' + enable_flag + '\n')
  fd.write('\n')
  fd.write('[auto-props]\n')
  fd.write('*.c = cfile=yes\n')
  fd.write('*.jpg = jpgfile=ja\n')
  fd.write('fubar* = tarfile=si\n')
  fd.write('foobar.lha = lhafile=da;lzhfile=niet\n')
  fd.write('* = auto=oui\n')
  fd.write('\n')
  fd.close()

  # create the file 'server'
  fd = open(cfgfile_srv, 'w')
  fd.write('#\n')
  fd.close()


#----------------------------------------------------------------------

def create_test_file(dir, name):
  "create a test file"

  fd = open(os.path.join(dir, name), 'w', 0644)
  fd.write('foo\nbar\nbaz\n')
  fd.close()

#----------------------------------------------------------------------

def autoprops_test(sbox, cmd, cfgtype, paramtype, subdir):
  """configurable autoprops test.
     if CMD == 1 do test svn import else test svn add
     if CFGTYPE == 1 add is enabled in the config, if it is 2 import is
       enabled else both are disabled
     if PARAMTYPE == 1 --auto-props is added to the commandline, if it is
       2 --no-auto-props is added else none is added
     if string SUBDIR is not empty files are created in that subdir and the
       directory is added/imported"""

  # some directories
  wc_dir = sbox.wc_dir
  tmp_dir = os.path.abspath(svntest.main.temp_dir)
  config_dir = os.path.join(tmp_dir, 'autoprops_config')
  repos_url = svntest.main.current_repo_url
  svntest.main.set_config_dir(config_dir)

  # initialize parameters
  parameters = []

  # add svn command
  if cmd == 1:
    parameters = parameters + ['import', '--username', main.wc_author,
                            '--password', main.wc_passwd, '-m', 'bla']
    need_svn_up = 1
    files_dir = tmp_dir
  else:
    parameters = parameters + ['add']
    need_svn_up = 0
    files_dir = wc_dir
  parameters = parameters + ['--config-dir', config_dir]

  # set config flags
  if cfgtype == 1:
    create_config(config_dir, 'yes')
    enable_flag = 1
  else:
    create_config(config_dir, 'no')
    enable_flag = 0

  # add comandline flags
  if paramtype == 1:
    parameters = parameters + ['--auto-props']
    enable_flag = 1
  elif paramtype == 2:
    parameters = parameters + ['--no-auto-props']
    enable_flag = 0

  # setup subdirectory if needed
  if len(subdir) > 0:
    files_dir = os.path.join(files_dir, subdir)
    files_wc_dir = os.path.join(wc_dir, subdir)
    os.makedirs(files_dir)
  else:
    files_wc_dir = wc_dir

  # create test files
  filenames = []
  filenames = filenames + ['foo.h']
  create_test_file(files_dir, filenames[len(filenames)-1])
  filenames = filenames + ['foo.c']
  create_test_file(files_dir, filenames[len(filenames)-1])
  filenames = filenames + ['foo.jpg']
  create_test_file(files_dir, filenames[len(filenames)-1])
  filenames = filenames + ['fubar.tar']
  create_test_file(files_dir, filenames[len(filenames)-1])
  filenames = filenames + ['foobar.lha']
  create_test_file(files_dir, filenames[len(filenames)-1])

  if len(subdir) == 0:
    # add/import the files
    for filename in filenames:
      filename = os.path.join(files_dir, filename)
      if cmd == 1:
        tmp_params = parameters + [filename, os.path.join(repos_url, filename)]
      else:
        tmp_params = parameters + [filename]
      svntest.main.run_svn(None, *tmp_params)
  else:
    # add/import subdirectory
    if cmd == 1:
      parameters = parameters + [files_dir, repos_url]
    else:
      parameters = parameters + [files_wc_dir]
    svntest.main.run_svn(None, *parameters)

  # do an svn up if needed
  if need_svn_up:
    svntest.main.run_svn(None, 'update')

  # check the properties
  if enable_flag:
    filename = os.path.join(files_wc_dir, 'foo.h' )
    check_proplist(filename,['auto'])
    check_prop('auto', filename, ['oui'])
    filename = os.path.join(files_wc_dir, 'foo.c' )
    check_proplist(filename,['cfile', 'auto'])
    check_prop('auto', filename, ['oui'])
    check_prop('cfile', filename, ['yes'])
    filename = os.path.join(files_wc_dir, 'foo.jpg' )
    check_proplist(filename,['jpgfile', 'auto'])
    check_prop('auto', filename, ['oui'])
    check_prop('jpgfile', filename, ['ja'])
    filename = os.path.join(files_wc_dir, 'fubar.tar' )
    check_proplist(filename,['tarfile', 'auto'])
    check_prop('auto', filename, ['oui'])
    check_prop('tarfile', filename, ['si'])
    filename = os.path.join(files_wc_dir, 'foobar.lha' )
    check_proplist(filename,['lhafile', 'lzhfile', 'auto'])
    check_prop('auto', filename, ['oui'])
    check_prop('lhafile', filename, ['da'])
    check_prop('lzhfile', filename, ['niet'])
  else:
    filename = os.path.join(files_wc_dir, 'foo.h' )
    check_proplist(filename,[])
    filename = os.path.join(files_wc_dir, 'foo.c' )
    check_proplist(filename,[])
    filename = os.path.join(files_wc_dir, 'foo.jpg' )
    check_proplist(filename,[])
    filename = os.path.join(files_wc_dir, 'fubar.tar' )
    check_proplist(filename,[])
    filename = os.path.join(files_wc_dir, 'foobar.lha' )
    check_proplist(filename,[])


#----------------------------------------------------------------------

def autoprops_add_no_none(sbox):
  "add: config=no,  commandline=none"

  # Bootstrap
  sbox.build()

  # cmd=add, config=no, commandline=none
  autoprops_test(sbox, 'add', 0, 0, '')

#----------------------------------------------------------------------

def autoprops_add_yes_none(sbox):
  "add: config=yes, commandline=none"

  # Bootstrap
  sbox.build()

  # cmd=add, config=yes, commandline=none
  autoprops_test(sbox, 'add', 1, 0, '')

#----------------------------------------------------------------------

def autoprops_add_no_yes(sbox):
  "add: config=no,  commandline=yes"

  # Bootstrap
  sbox.build()

  # cmd=add, config=no, commandline=yes
  autoprops_test(sbox, 'add', 0, 1, '')

#----------------------------------------------------------------------

def autoprops_add_yes_yes(sbox):
  "add: config=yes, commandline=yes"

  # Bootstrap
  sbox.build()

  # cmd=add, config=yes, commandline=yes
  autoprops_test(sbox, 'add', 1, 1, '')

#----------------------------------------------------------------------

def autoprops_add_no_no(sbox):
  "add: config=no,  commandline=no"

  # Bootstrap
  sbox.build()

  # cmd=add, config=no, commandline=no
  autoprops_test(sbox, 'add', 0, 2, '')

#----------------------------------------------------------------------

def autoprops_add_yes_no(sbox):
  "add: config=yes, commandline=no"

  # Bootstrap
  sbox.build()

  # cmd=add, config=yes, commandline=no
  autoprops_test(sbox, 'add', 1, 2, '')

#----------------------------------------------------------------------

def autoprops_imp_no_none(sbox):
  "import: config=no,  commandline=none"

  # Bootstrap
  sbox.build()

  # cmd=import, config=no, commandline=none
  autoprops_test(sbox, 'import', 0, 0, '')

#----------------------------------------------------------------------

def autoprops_imp_yes_none(sbox):
  "import: config=yes, commandline=none"

  # Bootstrap
  sbox.build()

  # cmd=import, config=yes, commandline=none
  autoprops_test(sbox, 'import', 1, 0, '')

#----------------------------------------------------------------------

def autoprops_imp_no_yes(sbox):
  "import: config=no,  commandline=yes"

  # Bootstrap
  sbox.build()

  # cmd=import, config=no, commandline=yes
  autoprops_test(sbox, 'import', 0, 1, '')

#----------------------------------------------------------------------

def autoprops_imp_yes_yes(sbox):
  "import: config=yes, commandline=yes"

  # Bootstrap
  sbox.build()

  # cmd=import, config=yes, commandline=yes
  autoprops_test(sbox, 'import', 1, 1, '')

#----------------------------------------------------------------------

def autoprops_imp_no_no(sbox):
  "import: config=no,  commandline=no"

  # Bootstrap
  sbox.build()

  # cmd=import, config=no, commandline=no
  autoprops_test(sbox, 'import', 0, 2, '')

#----------------------------------------------------------------------

def autoprops_imp_yes_no(sbox):
  "import: config=yes, commandline=no"

  # Bootstrap
  sbox.build()

  # cmd=import, config=yes, commandline=no
  autoprops_test(sbox, 'import', 1, 2, '')

#----------------------------------------------------------------------

def autoprops_add_dir(sbox):
  "add directory"

  # Bootstrap
  sbox.build()

  # cmd=import, config=yes, commandline=no
  autoprops_test(sbox, 'add', 1, 0, 'autodir')

#----------------------------------------------------------------------

def autoprops_imp_dir(sbox):
  "import directory"

  # Bootstrap
  sbox.build()

  # cmd=import, config=yes, commandline=no
  autoprops_test(sbox, 'import', 1, 0, 'autodir')


########################################################################
# Run the tests


# list all tests here, starting with None:
test_list = [ None,
              autoprops_add_no_none,
              autoprops_add_yes_none,
              autoprops_add_no_yes,
              autoprops_add_yes_yes,
              autoprops_add_no_no,
              autoprops_add_yes_no,
              autoprops_imp_no_none,
              autoprops_imp_yes_none,
              autoprops_imp_no_yes,
              autoprops_imp_yes_yes,
              autoprops_imp_no_no,
              autoprops_imp_yes_no,
              autoprops_add_dir,
              autoprops_imp_dir,
             ]

if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED


### End of file.
