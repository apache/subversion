#!/usr/bin/env python

import sys     # for argv[]
import os
import getopt
import shutil  # for rmtree()
import re
import stat    # for ST_MODE
import string  # for atof()
import copy    # for deepcopy()
import time    # for time()
import tempfile
import os400
import svntest
from svntest import Failure
from svntest import Skip
from svntest import testcase
from svntest import wc

scratch_path = os.path.join(os.getcwd(), 'scratch')
if not os.path.exists(scratch_path):
  os.mkdir(scratch_path)

try:
  my_getopt = getopt.gnu_getopt
except AttributeError:
  my_getopt = getopt.getopt

def os400_list_from_utf8(list):
  # Convert a list of strings from utf-8 to ebcdic.
  list_native = []
  for line in list:
    list_native.append((line.decode('utf-8')).encode('cp037'))
  return list_native

def os400_split_utf8_lines(one_big_line):
  # Helper function to deal with this common problem:
  #
  #   f = open(path, "rb")  # Where path is a utf-8 encoded file.
  #   lines = f.readlines() # We end up with all content in
  #                         # the list's first item.
  #
  # This function splits one_big_line[0] on '\n' into list.
  # Can also handle a string argument in the same manner.
  if type(one_big_line) is list:
    one_big_line = one_big_line[0]
  lines_tmp = one_big_line.split('\n'.encode('utf-8'))
  lines = []
  for line in lines_tmp:
    # If one_big_line ends with a newline, one_big_line.split will create
    # a list with an empty string as the last element.  We disregard
    # this empty element as it causes problems for code that works with the
    # size of the returned list (it's one element too big).
    if line != '':
      lines.append(line + '\n'.encode('utf-8'))
  return lines

def os400_spool_print(s, size=80, newline=True):
  # Print string s inserting a newline after every size chars.
  # Prevents errors when print(ing) long lines to spool files.
  remaining = len(s)
  start = 0
  end = remaining
  while(remaining > size):
    print s[start:start + size]
    start += size
    remaining = len(s) - start
  if newline:
    print s[start:end]
  else:
    print s[start:end],

def os400_tagtree(root_path, ccsid, rootonly=0):
  # Recursively tag files in a directory tree rooted at root_path with CCSID,
  # unless rootonly is true, then tag only the file at root_path or do
  # nothing if if root_path is a directory.
  import svntest
  from svntest import main

  if not os.path.exists(root_path):
    return
  elif os.path.isdir(root_path):
    names = os.listdir(root_path)
  else:
    names = [os.path.basename(root_path)]
    root_path = os.path.dirname(root_path)

  # Ensure permissions allow change of ccsid
  try:
    main.chmod_tree(root_path, 0666, 0666)
  except OSError:
    print "WARNING: os400_tagtree failed to set permissions 0666 0666 on '" + root_path + "'"

  errors = []
  for name in names:
    target = os.path.join(root_path, name)
    try:
      if os.path.isdir(target):
        if rootonly:
          return
        else:
          os400_tagtree(target, ccsid)
      else:
        qsh_set_ccsid_cmd = "QSYS/QSH CMD('setccsid " + str(ccsid) + " \"" + target + "\"')"
        os.system(qsh_set_ccsid_cmd)
    except (IOError, os.error), why:
      errors.append((target, qsh_set_ccsid_cmd, why))
  if errors:
    raise Error, errors


def os400_run_cmd_list(command, stdin_lines=None, out_utf8=0, err_utf8=0, va=[]):
  # Substitute for os.popen3 which is not implemented in iSeries Python.
  # Run qsh command with varargs and the list stdin_lines used as stdin.
  #
  # If out_utf8 or err_utf8 are true, then the temporary files created
  # which contain the command's stdout and stderr respectively are tagged
  # with a ccsid of 1208 to facilitate trouble shooting.

  # Return the stdout and stderr from the command as lists and the file
  # names of the temp files containing the stdout and stderr.

  # Let's try making all scratch files 1208 since UTF support in 
  out_utf8=1
  err_utf8=1

  ebcdic_stdout = False

  if stdin_lines:
    fin, in_file = tempfile.mkstemp('.in.txt', 'cmd.', scratch_path)
    finp = open(in_file, "wb")
    finp.writelines(stdin_lines)
    finp.close()
    # Will we ever not want the in_file tagged as 1208?
    qsh_set_ccsid_cmd = "QSYS/QSH CMD('setccsid 1208 " + in_file + "')"
    os.system(qsh_set_ccsid_cmd)

  fout, out_file = tempfile.mkstemp('.out.txt', 'cmd.', scratch_path)
  ferr, err_file = tempfile.mkstemp('.err.txt', 'cmd.', scratch_path)

  os.close(fout)
  os.close(ferr)

  # Does the caller want the temp files tagged with 1208?
  if (out_utf8):
    qsh_set_ccsid_cmd = "QSYS/QSH CMD('setccsid 1208 " + out_file + "')"
    os.system(qsh_set_ccsid_cmd)
  if (err_utf8):
    qsh_set_ccsid_cmd = "QSYS/QSH CMD('setccsid 1208 " + err_file + "')"
    os.system(qsh_set_ccsid_cmd)

  qshcmd = "QSYS/QSH CMD('" + command + " "

  arg_str = []

  counter = 1
  for arg in va:
    if (str(arg) == '>' or str(arg) == '<'):
      qshcmd = qshcmd + ' ' + str(arg) + ' '
    else:
      qshcmd = qshcmd + ' "' + str(arg) + '"'

  if stdin_lines:
    qshcmd += " < " + in_file

  qshcmd = qshcmd + " > " + out_file + " 2>" + err_file + "')"

  # Run the command via qsh
  os.system(qshcmd)

  solines = [] 
  if os.stat(out_file)[stat.ST_SIZE] > 0:
    solog = open(out_file, 'rb') 
    # Using .readlines() is ok for ebcdic files, but it doesn't work
    # for utf-8 files - it reads in the entire file as one line.
    if (out_utf8):
      so_contents = solog.read()
      try:
        # Some stdout won't be in utf-8, svn diff, svn pg of non-svn:* props, etc.
        # So do a quick test for utf-8-ness
        so_contents.decode('utf-8')
        my_getopt = getopt.gnu_getopt
        if so_contents.endswith('\n'.encode('utf-8')):
          ends_w_newline = True
        else:
          ends_w_newline = False
        solines_tmp = so_contents.split('\n'.encode('utf-8'))
        for line in solines_tmp:
          solines.append(line + '\n'.encode('utf-8'))
        if not ends_w_newline:
          solines[len(solines) - 1] = (solines[len(solines) - 1]).rstrip('\n'.encode('utf-8'))
        else:
          solines.pop()
      except UnicodeDecodeError:
        print 'DEBUG: *********************************************'
        print 'DEBUG: Command: ' + command
        print 'DEBUG: returning non-utf-8 stdout stored in file: ' + out_file
        os400_spool_print(so_contents)
        print 'DEBUG: *********************************************'
        ebcdic_stdout = True
        solines = so_contents.split('\n')
    else:
      solines = solog.readlines()
    solog.close()

  selines = [] 
  if os.stat(err_file)[stat.ST_SIZE] > 0:
    selog = open(err_file, 'rb')         
    if (err_utf8):
      se_contents = selog.read()
      if se_contents.endswith('\n'.encode('utf-8')):
        ends_w_newline = True
      else:
        ends_w_newline = False
      # Can we use se_contents.splitlines(1) somehow even though output is utf-8?
      selines_tmp = se_contents.split('\n'.encode('utf-8'))
      if selines_tmp != ['']:
        for line in selines_tmp:
          selines.append(line + '\n'.encode('utf-8'))
        if not ends_w_newline:
          selines[len(selines) - 1] = (selines[len(selines) - 1]).rstrip('\n'.encode('utf-8'))
        else:
          selines.pop()
    else:
      selines = selog.readlines() 
    selog.close()

  #solog.close()
  #selog.close()
  ### TODO: Delete these temp files, or use alternate function that cleans
  ### them up automagically.  For now we'll just remove the empty files to
  ### facilitate debugging.
  if os.stat(out_file)[stat.ST_SIZE] == 0:
    os.remove(out_file)
  if os.stat(err_file)[stat.ST_SIZE] == 0:
    os.remove(err_file)

  # Let's return ebcdic output for everything for now:
  stdoutlines = []
  stderrlines = []
  #print 'qshcmd:'
  #os400_spool_print(qshcmd)
  if not ebcdic_stdout:
    for line in solines:
      stdoutlines.append(line.decode('utf-8').encode('cp037'))
  else:
    stdoutlines = solines
  for line in selines:
    stderrlines.append(line.decode('utf-8').encode('cp037'))
  #return stdoutlines, selines, out_file, err_file
  return stdoutlines, stderrlines, out_file, err_file

def os400_run_cmd_va(command, stdin_lines=None, out_utf8=0, err_utf8=0, *varargs):
  # Same as os400_run_cmd_list but accepts variable args.
  arg_str = []
  for arg in varargs:
    arg_str.append(str(arg))
  return os400_run_cmd_list(command, stdin_lines, out_utf8, err_utf8, arg_str)


def os400_py_via_qshsys(script_path, opts=None):
  # Run python script at script_path.

  # Use .txt extensions for temp files so WebSphere can open them without difficulty.
  fout, out_file = tempfile.mkstemp('.out.txt', 'py.', scratch_path)
  ferr, err_file = tempfile.mkstemp('.err.txt', 'py.', scratch_path)
  fpy, py_file   = tempfile.mkstemp('.py.txt', 'py.', scratch_path)

  script = open(py_file, 'w')
  script_contents = "system \"PYTHON233/PYTHON PROGRAM(\'" + script_path + "\') "

  if opts:
    script_contents += "PARM("
    for o in opts:
      script_contents += "\'" + o + "\' "
    script_contents += ")\""
  else:
    script_contents += "\""

  script.write(script_contents)
  script.close()

  # Make tempfile executable
  os.chmod(py_file, stat.S_IEXEC | stat.S_IREAD | stat.S_IWRITE)

  failed = os.system("QSYS/QSH CMD('" + py_file + ">" + out_file + " 2>" + err_file + "')")

  solog = open(out_file, 'rb')
  selog = open(err_file, 'rb')
  solines = solog.readlines()
  selines = selog.readlines()
  solog.close()
  selog.close()

  ### TODO: Delete these temp files, or use alternate function that cleans them up automagically.
  ### For now we'll just remove the empty files to facilitate debugging.
  if os.stat(out_file)[stat.ST_SIZE] == 0:
    os.remove(out_file)
  if os.stat(err_file)[stat.ST_SIZE] == 0:
    os.remove(err_file)

  return failed, solines, selines


def os400_py_get_ccsid(path):
  # Get the ccsid of the file at path or -1 if a dir or non-existent.

  # If path is a dir or doesn't exist we are done.
  if os.path.isdir(path) or not os.path.exists(path):
    return -1

  # Use a qsh command to obtain the ccsid of file at path.
  qsh_set_ccsid_cmd = 'attr -p ' + path + ' CCSID'
  solines, selines, out_file, err_file = os400_run_cmd_list(qsh_set_ccsid_cmd)

  if selines:
    # If there is an error return 0.
    return 0
  else:
    # Else parse the ccsid from the output "CCSID=nnnn"
    return int(solines[0][6:]) 