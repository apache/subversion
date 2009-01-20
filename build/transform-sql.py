#!/usr/bin/env python
#
# transform-sql.py -- create a header file with the appropriate SQL variables
# from an SQL file
#


import os
import re
import sys


def usage_and_exit(msg):
  if msg:
    sys.stderr.write("%s\n\n" % msg)
  sys.stderr.write("usage: %s [sqlite_file]\n" % \
    os.path.basename(sys.argv[0]))
  sys.stderr.flush()
  sys.exit(1)


def main(input_filename, output_filename):
  input = open(input_filename, "r")
  output = open(output_filename, "w")

  var_name = os.path.basename(input_filename).replace('.', '_')
  var_name = var_name.replace('-', '_')

  output.write('static const char * const %s[] = { NULL,\n' % var_name)

  in_comment = False
  for line in input:
    line = line.replace('\n', '')
    line = line.replace('"', '\\"')

    if line:
      output.write('  "' + line + '"\n')
    else:
      output.write('  APR_EOL_STR\n')

  output.write('  };')

  input.close()
  output.close()


if __name__ == '__main__':
  if len(sys.argv) < 2:
    usage_and_exit("Incorrect number of arguments")
  main(sys.argv[1], sys.argv[1] + ".h")
