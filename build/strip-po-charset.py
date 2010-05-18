#!/usr/bin/env python
#
# strip-po-charset.py
#

import sys

def strip_po_charset(inp, out):

    out.write(inp.read().replace("\"Content-Type: text/plain; charset=UTF-8\\n\"\n",""))

def main():

    if len(sys.argv) != 3:
        print("Usage: %s <input (po) file> <output (spo) file>" % sys.argv[0])
        print("")
        print("Unsupported number of arguments; 2 required.")
        sys.exit(1)

    strip_po_charset(open(sys.argv[1],'r'), open(sys.argv[2],'w'))

if __name__ == '__main__':
    main()
