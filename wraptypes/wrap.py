#!/usr/bin/env python

'''Generate a Python ctypes wrapper file for a header file.

Usage example::
    wrap.py -lGL -oGL.py /usr/include/GL/gl.h

    >>> from GL import *

'''

__docformat__ = 'restructuredtext'
__version__ = '$Id: wrap.py 738 2007-03-12 04:53:42Z Alex.Holkner $'

from ctypesparser import *
import textwrap
import sys, re
from ctypes import CDLL, RTLD_LOCAL, RTLD_GLOBAL
from ctypes.util import find_library

def load_library(name, mode=RTLD_LOCAL):
    if os.name == "nt":
        return CDLL(name, mode=mode)
    path = find_library(name)
    if path is None:
        # Maybe 'name' is not a library name in the linker style,
        # give CDLL a last chance to find the library.
        path = name
    return CDLL(path, mode=mode)

class CtypesWrapper(CtypesParser, CtypesTypeVisitor):
    file=None
    def begin_output(self, output_file, library, link_modules=(), 
                     emit_filenames=(), all_headers=False,
                     include_symbols=None, exclude_symbols=None):
        self.library = library
        self.file = output_file
        self.all_names = []
        self.known_types = {}
        self.structs = set()
        self.enums = set()
        self.emit_filenames = emit_filenames
        self.all_headers = all_headers
        self.include_symbols = None
        self.exclude_symbols = None
        if include_symbols:
            self.include_symbols = re.compile(include_symbols)
        if exclude_symbols:
            self.exclude_symbols = re.compile(exclude_symbols)

        self.linked_symbols = {}
        for name in link_modules:
            module = __import__(name, globals(), locals(), ['foo'])
            for symbol in dir(module):
                if symbol not in self.linked_symbols:
                    self.linked_symbols[symbol] = '%s.%s' % (name, symbol)
        self.link_modules = link_modules

        self.print_preamble()
        self.print_link_modules_imports()

    def wrap(self, filename, source=None):
        assert self.file, 'Call begin_output first'
        self.parse(filename, source)

    def end_output(self):
        self.print_epilogue()
        self.file = None

    def does_emit(self, symbol, filename, restype=None, argtypes=[]):

        # Skip any functions which mention excluded types
        if self.exclude_symbols:
            for a in [symbol, restype] + argtypes:
                if a and self.exclude_symbols.match(str(a)):
                    return

        if self.include_symbols:
            return re.match(self.include_symbols, symbol)

        return self.all_headers or filename in self.emit_filenames

    def print_preamble(self):
        import textwrap
        import time
        print >> self.file, textwrap.dedent("""
            '''Wrapper for %(library)s
            
            Generated with:
            %(argv)s
            
            Do not modify this file.
            '''

            __docformat__ =  'restructuredtext'
            __version__ = '$Id: wrap.py 738 2007-03-12 04:53:42Z Alex.Holkner $'

            import ctypes
            from ctypes import *

            _int_types = (c_int16, c_int32)
            if hasattr(ctypes, 'c_int64'):
                # Some builds of ctypes apparently do not have c_int64
                # defined; it's a pretty good bet that these builds do not
                # have 64-bit pointers.
                _int_types += (ctypes.c_int64,)
            for t in _int_types:
                if sizeof(t) == sizeof(c_size_t):
                    c_ptrdiff_t = t

            class c_void(Structure):
                # c_void_p is a buggy return type, converting to int, so
                # POINTER(None) == c_void_p is actually written as
                # POINTER(c_void), so it can be treated as a real pointer.
                _fields_ = [('dummy', c_int)]

            _libs = {}

        """ % {
            'library': str(self.library),
            'date': time.ctime(),
            'class': self.__class__.__name__,
            'argv': ' '.join(sys.argv),
        }).lstrip()
        self.loaded_libraries = []
        for library in self.library:
            lib = load_library(library)
            if lib:
                self.loaded_libraries.append(lib)
                print >>self.file, textwrap.dedent("""
                    _libs[%r] = cdll.LoadLibrary(%r)
                """ % (lib._name, lib._name))

    def print_link_modules_imports(self):
        for name in self.link_modules:
            print >> self.file, 'import %s' % name
        print >> self.file

    def print_epilogue(self):
        print >> self.file
        print >> self.file,  '\n'.join(textwrap.wrap(
            '__all__ = [%s]' % ', '.join([repr(n) for n in self.all_names]),
            width=78,
            break_long_words=False))


    def handle_ctypes_constant(self, name, value, filename, lineno):
        if self.does_emit(name, filename):
            print >> self.file, '%s = %r' % (name, value),
            print >> self.file, '\t# %s:%d' % (filename, lineno)
            self.all_names.append(name)

    def handle_ctypes_type_definition(self, name, ctype, filename, lineno):
        if self.does_emit(name, filename):
            self.all_names.append(name)
            if name in self.linked_symbols:
                print >> self.file, '%s = %s' % \
                    (name, self.linked_symbols[name])
            else:
                ctype.visit(self)
                self.emit_type(ctype)
                print >> self.file, '%s = %s' % (name, str(ctype)),
                print >> self.file, '\t# %s:%d' % (filename, lineno)
        else:
            self.known_types[name] = (ctype, filename, lineno)

    def emit_type(self, t):
        t.visit(self)
        for s in t.get_required_type_names():
            if s in self.known_types:
                if s in self.linked_symbols:
                    print >> self.file, '%s = %s' % (s, self.linked_symbols[s])
                else:
                    s_ctype, s_filename, s_lineno = self.known_types[s]
                    s_ctype.visit(self)

                    self.emit_type(s_ctype)
                    print >> self.file, '%s = %s' % (s, str(s_ctype)),
                    print >> self.file, '\t# %s:%d' % (s_filename, s_lineno)
                del self.known_types[s]

    def visit_struct(self, struct):
        if struct.tag in self.structs:
            return
        self.structs.add(struct.tag)
            
        base = {True: 'Union', False: 'Structure'}[struct.is_union]
        print >> self.file, 'class struct_%s(%s):' % (struct.tag, base)
        print >> self.file, '    __slots__ = ['
        if not struct.opaque:
            for m in struct.members:
                print >> self.file, "        '%s'," % m[0]
        print >> self.file, '    ]'

        # Set fields after completing class, so incomplete structs can be
        # referenced within struct.
        for name, typ in struct.members:
            self.emit_type(typ)

        print >> self.file, 'struct_%s._fields_ = [' % struct.tag
        if struct.opaque:
            print >> self.file, "    ('_opaque_struct', c_int)"
            self.structs.remove(struct.tag)
        else:
            for m in struct.members:
                print >> self.file, "    ('%s', %s)," % (m[0], m[1])
        print >> self.file, ']'
        print >> self.file

    def visit_enum(self, enum):
        if enum.tag in self.enums:
            return
        self.enums.add(enum.tag)

        print >> self.file, 'enum_%s = c_int' % enum.tag
        for name, value in enum.enumerators:
            self.all_names.append(name)
            print >> self.file, '%s = %d' % (name, value)

    def handle_ctypes_function(self, name, restype, argtypes, filename, lineno):
        if self.does_emit(name, filename, restype, argtypes):

            # Also emit any types this func requires that haven't yet been
            # written.
            self.emit_type(restype)
            for a in argtypes:
                self.emit_type(a)

            for lib in self.loaded_libraries:
                if hasattr(lib, name):
                    self.all_names.append(name)
                    print >> self.file, '# %s:%d' % (filename, lineno)
                    print >> self.file, '%s = _libs[%r].%s' % (name, lib._name, name)
                    print >> self.file, '%s.restype = %s' % (name, str(restype))
                    print >> self.file, '%s.argtypes = [%s]' % \
                        (name, ', '.join([str(a) for a in argtypes])) 
                    print >> self.file
                    break

    def handle_ctypes_variable(self, name, ctype, filename, lineno):
        # This doesn't work.
        #self.all_names.append(name)
        #print >> self.file, '%s = %s.indll(_lib, %r)' % \
        #    (name, str(ctype), name)
        pass

def main(*argv):
    import optparse
    import sys
    import os.path

    usage = 'usage: %prog [options] <header.h>'
    op = optparse.OptionParser(usage=usage)
    op.add_option('-o', '--output', dest='output',
                  help='write wrapper to FILE', metavar='FILE')
    op.add_option('-l', '--library', dest='library', action='append',
                  help='link to LIBRARY', metavar='LIBRARY', default=[])
    op.add_option('-I', '--include-dir', action='append', dest='include_dirs',
                  help='add DIR to include search path', metavar='DIR',
                  default=[])
    op.add_option('-m', '--link-module', action='append', dest='link_modules',
                  help='use symbols from MODULE', metavar='MODULE',
                  default=[])
    op.add_option('-a', '--all-headers', action='store_true',
                  dest='all_headers',
                  help='include symbols from all headers', default=False)
    op.add_option('-i', '--include-symbols', dest='include_symbols',
                  help='regular expression for symbols to include')
    op.add_option('-x', '--exclude-symbols', dest='exclude_symbols',
                  help='regular expression for symbols to exclude')
    op.add_option('-D', '--defines', dest='defines', action='append',
                  help='preprocessor define (not supported)', default=[])
    
    (options, args) = op.parse_args(list(argv[1:]))
    if len(args) < 1:
        print >> sys.stderr, 'No header files specified.'
        sys.exit(1)
    headers = args

    if options.output is None:
        print >> sys.stderr, 'No output file specified.'
        sys.exit(1)

    if len(options.library) == 0:
        print >> sys.stderr, 'No libraries specified.'

    wrapper = CtypesWrapper()
    wrapper.begin_output(open(options.output, 'w'), 
                         library=options.library, 
                         emit_filenames=headers,
                         link_modules=options.link_modules,
                         all_headers=options.all_headers,
                         include_symbols=options.include_symbols,
                         exclude_symbols=options.exclude_symbols)
    wrapper.preprocessor_parser.include_path += options.include_dirs
    for header in headers:
        wrapper.wrap(header)
    wrapper.end_output()

    print 'Wrapped to %s' % options.output

if __name__ == '__main__':
    main(*sys.argv)
