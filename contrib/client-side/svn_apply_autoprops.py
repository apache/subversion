#!/usr/bin/env python

# This script reads the auto-proprieties defined in the
# $HOME/.subversion/config file and applies them recursively to all
# the files and directories in the current working copy.  It may
# behave differently than the Subversion command line; where the
# subversion command line may only apply a single matching
# auto-property to a single pathname, this script will apply all
# matching lines to a single pathname.
#
# To do:
# 1) Switch to using the Subversion Python bindings.
# 2) Only delete files and directories if the --delete command line
#    option is present.
# 3) Use a command line option to specify the configuration file to
#    load the auto-properties from.
#
# $Herders: http://www.orcaware.com/svn/repos/trunk/orca/orca/orca.pl.in $
# $LastChangedRevision$
# $LastChangedDate$
# $LastChangedBy$
#
# Copyright (C) 2005 Blair Zajac.
#
# This script is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This script is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Orca in the COPYING-GPL file; if not, write to the Free
# Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
# 02111-1307 USA

import fnmatch
import os
import re
import sys

# The path to the Subversion configuration file.
SVN_CONFIG_FILENAME = '$HOME/.subversion/config'

# The name of Subversion's private directory in working copies.
SVN_WC_ADM_DIR_NAME = '.svn'

def get_autoprop_lines(fd):
  lines = []
  reading_autoprops = 0

  re_start_autoprops = re.compile('^\s*\[auto-props\]\s*')
  re_end_autoprops = re.compile('^\s*\[\w+\]\s*')

  for line in fd.xreadlines():
    if reading_autoprops:
      if re_end_autoprops.match(line):
        reading_autoprops = 0
        continue
    else:
      if re_start_autoprops.match(line):
        reading_autoprops = 1
        continue

    if reading_autoprops:
      lines += [line]

  return lines

re_remove_leading_whitespace = re.compile('^\s+')
re_remove_trailing_whitespace = re.compile('\s+$')

def process_autoprop_lines(lines):
  result = []

  for line in lines:
    # Split the line on the = separating the fnmatch string from the
    # properties.
    try:
      (fnmatch, props) = line.split('=', 1)
    except ValueError:
      continue

    # Remove leading and trailing whitespace from the fnmatch and
    # properties.
    fnmatch = re.sub(re_remove_leading_whitespace, '', fnmatch)
    fnmatch = re.sub(re_remove_trailing_whitespace, '', fnmatch)
    props = re.sub(re_remove_leading_whitespace, '', props)
    props = re.sub(re_remove_trailing_whitespace, '', props)

    # Create a list of property name and property values.
    props_list = []
    for prop in props.split(';'):
      if not len(prop):
        continue
      try:
        (prop_name, prop_value) = prop.split('=', 1)
      except ValueError:
        prop_name = prop
        prop_value = '*'
      if len(prop_name):
        props_list += [(prop_name, prop_value)]

    result += [(fnmatch, props_list)]

  return result

def filter_walk(autoprop_lines, dirname, filenames):
  # Do no descend into directories that do not have a .svn directory.
  try:
    filenames.remove(SVN_WC_ADM_DIR_NAME)
  except ValueError:
    filenames = []
    print "Will not process files in '%s' because it does not have a '%s' " \
          "directory." \
          % (dirname, SVN_WC_ADM_DIR_NAME)
    return

  filenames.sort()

  # Find those filenames that match each fnmatch.
  for autoprops_line in autoprop_lines:
    fnmatch_str = autoprops_line[0]
    prop_list = autoprops_line[1]

    matching_filenames = fnmatch.filter(filenames, fnmatch_str)
    if not matching_filenames:
      continue

    for prop in prop_list:
      command = ['svn', 'propset', prop[0], prop[1]]
      for f in matching_filenames:
        command += ["%s/%s" % (dirname, f)]

      status = os.spawnvp(os.P_WAIT, 'svn', command)
      if status:
        print 'Command "%s" failed with exit status %s' \
              % (command, status)
        sys.exit(1)

def main():
  config_filename = os.path.expandvars(SVN_CONFIG_FILENAME)
  try:
    fd = file(config_filename)
  except IOError:
    print "Cannot open svn configuration file '%s' for reading: %s" \
          % (config_filename, sys.exc_value.strerror)

  autoprop_lines = get_autoprop_lines(fd)

  fd.close()

  autoprop_lines = process_autoprop_lines(autoprop_lines)

  os.path.walk('.', filter_walk, autoprop_lines)

if __name__ == '__main__':
  sys.exit(main())
