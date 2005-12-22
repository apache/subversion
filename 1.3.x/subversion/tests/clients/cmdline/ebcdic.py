#!/usr/bin/env python

import sys     # for argv[]
import os      # for popen2()
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

# Limited ebcdic to ascii conversion table.
e2a_table = [
[ ' ',  '\x00',  '\x00',  '\x00' ],
[ ' ',  '\x01',  '\x01',  '\x00' ],
[ ' ',  '\x02',  '\x02',  '\x00' ],
[ ' ',  '\x03',  '\x03',  '\x00' ],
[ ' ',  '\x04',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ ' ',  '\x05',  '\x09',  '\x00' ],
[ ' ',  '\x06',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ ' ',  '\x07',  '\x7f',  '\x00' ],
[ ' ',  '\x08',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ ' ',  '\x09',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ ' ',  '\x0a',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ ' ',  '\x0b',  '\x0b',  '\x00' ],
[ ' ',  '\x0c',  '\x0c',  '\x00' ],
[ ' ',  '\x0d',  '\x0d',  '\x00' ],
[ ' ',  '\x0e',  '\x0e',  '\x00' ],
[ ' ',  '\x0f',  '\x0f',  '\x00' ],
[ ' ',  '\x10',  '\x10',  '\x00' ],
[ ' ',  '\x11',  '\x11',  '\x00' ],
[ ' ',  '\x12',  '\x12',  '\x00' ],
[ ' ',  '\x13',  '\x13',  '\x00' ],
[ ' ',  '\x14',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ ' ',  '\x15',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ ' ',  '\x16',  '\x08',  '\x00' ],
[ ' ',  '\x17',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ ' ',  '\x18',  '\x18',  '\x00' ],
[ ' ',  '\x19',  '\x19',  '\x00' ],
[ ' ',  '\x1a',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ ' ',  '\x1b',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ ' ',  '\x1c',  '\x1c',  '\x00' ],
[ ' ',  '\x1d',  '\x1d',  '\x00' ],
[ ' ',  '\x1e',  '\x1e',  '\x00' ],
[ ' ',  '\x1f',  '\x1f',  '\x00' ],
[ ' ',  '\x20',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ ' ',  '\x21',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ ' ',  '\x22',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ ' ',  '\x23',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ ' ',  '\x24',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ ' ',  '\x25',  '\x0a',  '\x00' ],
[ ' ',  '\x26',  '\x17',  '\x00' ],
[ ' ',  '\x27',  '\x1b',  '\x00' ],
[ ' ',  '\x28',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ ' ',  '\x29',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ ' ',  '\x2a',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ ' ',  '\x2b',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ ' ',  '\x2c',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ ' ',  '\x2d',  '\x05',  '\x00' ],
[ ' ',  '\x2e',  '\x06',  '\x00' ],
[ ' ',  '\x2f',  '\x07',  '\x00' ],
[ ' ',  '\x30',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ ' ',  '\x31',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ ' ',  '\x32',  '\x16',  '\x00' ],
[ ' ',  '\x33',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ ' ',  '\x34',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ ' ',  '\x35',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ ' ',  '\x36',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ ' ',  '\x37',  '\x04',  '\x00' ],
[ ' ',  '\x38',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ ' ',  '\x39',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ ' ',  '\x3a',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ ' ',  '\x3b',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ ' ',  '\x3c',  '\x14',  '\x00' ],
[ ' ',  '\x3d',  '\x15',  '\x00' ],
[ ' ',  '\x3e',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ ' ',  '\x3f',  '\x1a',  '\x00' ],
[ ' ',  '\x40',  '\x20',  '\x00' ],
[ ' ',  '\x41',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ 'â',  '\x42',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ 'ä',  '\x43',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ 'à',  '\x44',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ 'á',  '\x45',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ '.',  '\x46',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ 'å',  '\x47',  '\x7d',  '\x00' ],
[ 'ç',  '\x48',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ 'ñ',  '\x49',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ '¢',  '\x4a',  '\x23',  '\x00' ],
[ '.',  '\x4b',  '\x2e',  '\x00' ],
[ '<',  '\x4c',  '\x3c',  '\x00' ],
[ '(',  '\x4d',  '\x28',  '\x00' ],
[ '+',  '\x4e',  '\x2b',  '\x00' ],
[ '|',  '\x4f',  '\x21',  '\x00' ],
[ '&',  '\x50',  '\x26',  '\x00' ],
[ 'é',  '\x51',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ 'ê',  '\x52',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ 'ë',  '\x53',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ 'è',  '\x54',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ 'í',  '\x55',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ 'î',  '\x56',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ 'ï',  '\x57',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ 'ì',  '\x58',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ 'ß',  '\x59',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ '!',  '\x5a',  '\x21',  '\x00' ], # Multibyte Conversion to UTF-8
[ '$',  '\x5b',  '\x24',  '\x00' ], # Multibyte Conversion to UTF-8
[ '*',  '\x5c',  '\x2a',  '\x00' ],
[ ')',  '\x5d',  '\x29',  '\x00' ],
[ ';',  '\x5e',  '\x3b',  '\x00' ],
[ '¬',  '\x5f',  '\x5e',  '\x00' ],
[ '-',  '\x60',  '\x2d',  '\x00' ],
[ '/',  '\x61',  '\x2f',  '\x00' ],
[ '.',  '\x62',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ 'Ä',  '\x63',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ '.',  '\x64',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ '.',  '\x65',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ '.',  '\x66',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ 'Å',  '\x67',  '\x24',  '\x00' ],
[ 'Ç',  '\x68',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ 'Ñ',  '\x69',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ '.',  '\x6a',  '\x7c',  '\x00' ], # Multibyte Conversion to UTF-8
[ ',',  '\x6b',  '\x2c',  '\x00' ],
[ '%',  '\x6c',  '\x25',  '\x00' ],
[ '_',  '\x6d',  '\x5f',  '\x00' ],
[ '>',  '\x6e',  '\x3e',  '\x00' ],
[ '?',  '\x6f',  '\x3f',  '\x00' ],
[ '.',  '\x70',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ 'É',  '\x71',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ '.',  '\x72',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ '.',  '\x73',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ '.',  '\x74',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ '.',  '\x75',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ '.',  '\x76',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ '.',  '\x77',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ '.',  '\x78',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ '`',  '\x79',  '\x60',  '\x00' ],
[ ':',  '\x7a',  '\x3a',  '\x00' ],
[ '#',  '\x7b',  '\x23',  '\x00' ], # Multibyte Conversion to UTF-8
[ '@',  '\x7c',  '\x40',  '\x00' ], # Multibyte Conversion to UTF-8
[ "'",  '0x7d',  '\x27',  '\x00' ],
[ '=',  '\x7e',  '\x3d',  '\x00' ],
[ '"',  '\x7f',  '\x22',  '\x00' ],
[ '.',  '\x80',  '\x40',  '\x00' ],
[ 'a',  '\x81',  '\x61',  '\x00' ],
[ 'b',  '\x82',  '\x62',  '\x00' ],
[ 'c',  '\x83',  '\x63',  '\x00' ],
[ 'd',  '\x84',  '\x64',  '\x00' ],
[ 'e',  '\x85',  '\x65',  '\x00' ],
[ 'f',  '\x86',  '\x66',  '\x00' ],
[ 'g',  '\x87',  '\x67',  '\x00' ],
[ 'h',  '\x88',  '\x68',  '\x00' ],
[ 'i',  '\x89',  '\x69',  '\x00' ],
[ '«',  '\x8a',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ '»',  '\x8b',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ '.',  '\x8c',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ '.',  '\x8d',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ '.',  '\x8e',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ '±',  '\x8f',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ '°',  '\x90',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ 'j',  '\x91',  '\x6a',  '\x00' ],
[ 'k',  '\x92',  '\x6b',  '\x00' ],
[ 'l',  '\x93',  '\x6c',  '\x00' ],
[ 'm',  '\x94',  '\x6d',  '\x00' ],
[ 'n',  '\x95',  '\x6e',  '\x00' ],
[ 'o',  '\x96',  '\x6f',  '\x00' ],
[ 'p',  '\x97',  '\x70',  '\x00' ],
[ 'q',  '\x98',  '\x71',  '\x00' ],
[ 'r',  '\x99',  '\x72',  '\x00' ],
[ 'ª',  '\x9a',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ 'º',  '\x9b',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ 'æ',  '\x9c',  '\x7b',  '\x00' ],
[ '.',  '\x9d',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ 'Æ',  '\x9e',  '\x5b',  '\x00' ],
[ '.',  '\x9f',  '\x5d',  '\x00' ],
[ '.',  '\xa0',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ '~',  '\xa1',  '\x7E',  '\x00' ], # Multibyte Conversion to UTF-8
[ 's',  '\xa2',  '\x73',  '\x00' ],
[ 't',  '\xa3',  '\x74',  '\x00' ],
[ 'u',  '\xa4',  '\x75',  '\x00' ],
[ 'v',  '\xa5',  '\x76',  '\x00' ],
[ 'w',  '\xa6',  '\x77',  '\x00' ],
[ 'x',  '\xa7',  '\x78',  '\x00' ],
[ 'y',  '\xa8',  '\x79',  '\x00' ],
[ 'z',  '\xa9',  '\x7a',  '\x00' ],
[ '¡',  '\xaa',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ '¿',  '\xab',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ '.',  '\xac',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ '.',  '\xad',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ '.',  '\xae',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ '.',  '\xaf',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ '^',  '\xb0',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ '£',  '\xb1',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ '¥',  '\xb2',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ '·',  '\xb3',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ '.',  '\xb4',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ '§',  '\xb5',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ '¶',  '\xb6',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ '¼',  '\xb7',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ '½',  '\xb8',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ '.',  '\xb9',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ '[',  '\xba',  '\x5b',  '\x00' ],
[ ']',  '\xbb',  '\x5d',  '\x00' ],
[ '.',  '\xbc',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ '.',  '\xbd',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ '.',  '\xbe',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ '.',  '\xbf',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ '{',  '\xc0',  '\x7b',  '\x00' ], # Multibyte Conversion to UTF-8
[ 'A',  '\xc1',  '\x41',  '\x00' ],
[ 'B',  '\xc2',  '\x42',  '\x00' ],
[ 'C',  '\xc3',  '\x43',  '\x00' ],
[ 'D',  '\xc4',  '\x44',  '\x00' ],
[ 'E',  '\xc5',  '\x45',  '\x00' ],
[ 'F',  '\xc6',  '\x46',  '\x00' ],
[ 'G',  '\xc7',  '\x47',  '\x00' ],
[ 'H',  '\xc8',  '\x48',  '\x00' ],
[ 'I',  '\xc9',  '\x49',  '\x00' ],
[ '.',  '\xca',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ 'ô',  '\xcb',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ 'ö',  '\xcc',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ 'ò',  '\xcd',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ 'ó',  '\xce',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ '.',  '\xcf',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ '}',  '\xd0',  '\x7d',  '\x00' ], # Multibyte Conversion to UTF-8
[ 'J',  '\xd1',  '\x4a',  '\x00' ],
[ 'K',  '\xd2',  '\x4b',  '\x00' ],
[ 'L',  '\xd3',  '\x4c',  '\x00' ],
[ 'M',  '\xd4',  '\x4d',  '\x00' ],
[ 'N',  '\xd5',  '\x4e',  '\x00' ],
[ 'O',  '\xd6',  '\x4f',  '\x00' ],
[ 'P',  '\xd7',  '\x50',  '\x00' ],
[ 'Q',  '\xd8',  '\x51',  '\x00' ],
[ 'R',  '\xd9',  '\x52',  '\x00' ],
[ '.',  '\xda',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ 'û',  '\xdb',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ 'ü',  '\xdc',  '\x7e',  '\x00' ],
[ 'ù',  '\xdd',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ 'ú',  '\xde',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ 'ÿ',  '\xdf',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ '\\', '0xe0',  '\x5c',  '\x00' ],
[ '÷',  '\xe1',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ 'S',  '\xe2',  '\x53',  '\x00' ],
[ 'T',  '\xe3',  '\x54',  '\x00' ],
[ 'U',  '\xe4',  '\x55',  '\x00' ],
[ 'V',  '\xe5',  '\x56',  '\x00' ],
[ 'W',  '\xe6',  '\x57',  '\x00' ],
[ 'X',  '\xe7',  '\x58',  '\x00' ],
[ 'Y',  '\xe8',  '\x59',  '\x00' ],
[ 'Z',  '\xe9',  '\x5a',  '\x00' ],
[ '²',  '\xea',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ '.',  '\xeb',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ 'Ö',  '\xec',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ '.',  '\xed',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ '.',  '\xee',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ '.',  '\xef',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ '0',  '\xf0',  '\x30',  '\x00' ],
[ '1',  '\xf1',  '\x31',  '\x00' ],
[ '2',  '\xf2',  '\x32',  '\x00' ],
[ '3',  '\xf3',  '\x33',  '\x00' ],
[ '4',  '\xf4',  '\x34',  '\x00' ],
[ '5',  '\xf5',  '\x35',  '\x00' ],
[ '6',  '\xf6',  '\x36',  '\x00' ],
[ '7',  '\xf7',  '\x37',  '\x00' ],
[ '8',  '\xf8',  '\x38',  '\x00' ],
[ '9',  '\xf9',  '\x39',  '\x00' ],
[ '.',  '\xfa',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ '.',  '\xfb',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ 'Ü',  '\xfc',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ '.',  '\xfd',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ '.',  '\xfe',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
[ ' ',  '\xff',  '\x00',  '\x00' ], # Multibyte Conversion to UTF-8
]

def os400_convert_string_to_utf8(s):
  # Convert ebcdic (ccsid 37) string s to utf-8.
  # Note: iSeries Python doesn't correctly convert variant
  #       characters to utf-8 with .encode('utf-8')
  #       necessitating this function.
  r = ''
  for i in range(len(s)):
    r += e2a_table[ord(s[i])][2]
  return r


def os400_list_from_utf8(list):
  # Convert a list of strings from utf-8 to ebcdic.
  list_native = []
  for line in list:
    list_native.append((line.decode('utf-8')).encode('cp500'))
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

  solog = open(out_file, 'rb')
  selog = open(err_file, 'rb')

  # Using .readlines() is ok for ebcdic files, but it doesn't work
  # for utf-8 files - it reads in the entire file as one line.
  if (out_utf8):
    so_contents = solog.read()
    if so_contents.endswith('\n'.encode('utf-8')):
      ends_w_newline = True
    else:
      ends_w_newline = False
    solines_tmp = so_contents.split('\n'.encode('utf-8'))
    solines = []
    for line in solines_tmp:
      solines.append(line + '\n'.encode('utf-8'))
    if not ends_w_newline:
      solines[len(solines) - 1] = (solines[len(solines) - 1]).rstrip('\n'.encode('utf-8'))
    else:
      solines.pop()
  else:
    solines = solog.readlines()

  if (err_utf8):
    se_contents = selog.read()
    if se_contents.endswith('\n'.encode('utf-8')):
      ends_w_newline = True
    else:
      ends_w_newline = False
    selines_tmp = se_contents.split('\n'.encode('utf-8'))
    selines = []
    for line in selines_tmp:
      selines.append(line + '\n'.encode('utf-8'))
    if not ends_w_newline:
      selines[len(selines) - 1] = (selines[len(selines) - 1]).rstrip('\n'.encode('utf-8'))
    else:
      selines.pop()
  else:
    selines = selog.readlines() 

  solog.close()
  selog.close()
  ### TODO: Delete these temp files, or use alternate function that cleans
  ### them up automagically.  For now we'll just remove the empty files to
  ### facilitate debugging.
  if os.stat(out_file)[stat.ST_SIZE] == 0:
    os.remove(out_file)
  if os.stat(err_file)[stat.ST_SIZE] == 0:
    os.remove(err_file)

  return solines, selines, out_file, err_file


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
