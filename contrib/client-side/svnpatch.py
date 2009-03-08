#!/usr/bin/env python

# svnpatch.py - svnpatch helper script
# Author: Arfrever Frehtes Taifersar Arahesis
# License: GPL-3

import base64
import sys
import textwrap
import zlib

svnpatch1_block_start = b"========================= SVNPATCH1 BLOCK =========================\n"

def encode():
	sys.stdout.write(svnpatch1_block_start.decode())
	sys.stdout.write("\n".join(textwrap.wrap(base64.encodestring(zlib.compress(b"\n".join([x.rstrip(b"\n") for x in lines]))).decode(), 76)))

def decode():
	svnpatch1_block_start_index = lines.index(svnpatch1_block_start)
	svnpatch1_block = lines[svnpatch1_block_start_index+1:]
	sys.stdout.write(zlib.decompress(base64.decodestring(b"".join([x.rstrip(b"\n") for x in svnpatch1_block]))).decode())

def help():
	print("svnpatch.py - svnpatch helper script")
	print("Usage: svnpatch.py [-e | --encode | -d | --decode] FILE")
	print("       svnpatch.py [-e | --encode | -d | --decode] -")
	print("       svnpatch.py [-h | --help]")
	print("")
	print("Author: Arfrever Frehtes Taifersar Arahesis")
	print("License: GPL-3")
	exit(0)

if len(sys.argv) == 2 and sys.argv[1] in ("-h", "--help"):
	help()

elif len(sys.argv) < 3:
	sys.stderr.write("svnpatch.py: Missing arguments\n")
	exit(1)

elif len(sys.argv) > 3:
	sys.stderr.write("svnpatch.py: Excessive arguments(s)\n")
	exit(1)

if sys.argv[1] in ("-e", "--encode"):
	func = encode
elif sys.argv[1] in ("-d", "--decode"):
	func = decode
else:
	sys.stderr.write("Incorrect option\n")
	exit(1)

if sys.argv[2] == "-":
	lines = sys.stdin.readlines()
	if sys.version_info[0] >= 3:
		lines = [x.encode() for x in lines]
else:
	lines = open(sys.argv[1], "rb").readlines()

func()
print("")
