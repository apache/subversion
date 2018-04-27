#!/usr/bin/env python
#
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#
#

import gdb
import re

import gdb.printing
from gdb.printing import RegexpCollectionPrettyPrinter


class TypedefRegexCollectionPrettyPrinter(RegexpCollectionPrettyPrinter):
    """Class for implementing a collection of pretty-printers, matching the
       type name to a regular expression.

       A pretty-printer in this collection will be used if the type of the
       value to be printed matches the printer's regular expression, or if
       the value is a pointer to and/or typedef to a type name that matches
       its regular expression.  The variations are tried in this order:

         1. the type name as known to the debugger (could be a 'typedef');
         2. the type after stripping off any number of layers of 'typedef';
         3. if it is a pointer, the pointed-to type;
         4. if it is a pointer, the pointed-to type minus some 'typedef's.

       In all cases, ignore 'const' and 'volatile' qualifiers.  When
       matching the pointed-to type, dereference the value or use 'None' if
       the value was a null pointer.

       This class is modeled on RegexpCollectionPrettyPrinter, which (in GDB
       7.3) matches on the base type's tag name and can't match a pointer
       type or any other type that doesn't have a tag name.
    """

    def __init__(self, name):
        super(TypedefRegexCollectionPrettyPrinter, self).__init__(name)

    def __call__(self, val):
        """Find and return an instantiation of a printer for VAL.
        """

        def lookup_type(type, val):
            """Return the first printer whose regular expression matches the
               name (tag name for struct/union/enum types) of TYPE, ignoring
               any 'const' or 'volatile' qualifiers.

               VAL is a gdb.Value, or may be None to indicate a dereferenced
               null pointer.  TYPE is the associated gdb.Type.
            """
            if type.code in [gdb.TYPE_CODE_STRUCT, gdb.TYPE_CODE_UNION,
                             gdb.TYPE_CODE_ENUM]:
                typename = type.tag
            else:
                typename = str(type.unqualified())
            for printer in self.subprinters:
                if printer.enabled and printer.compiled_re.search(typename):
                    return printer.gen_printer(val)

        def lookup_type_or_alias(type, val):
            """Return the first printer matching TYPE, or else if TYPE is a
               typedef then the first printer matching the aliased type.

               VAL is a gdb.Value, or may be None to indicate a dereferenced
               null pointer.  TYPE is the associated gdb.Type.
            """
            # First, look for a printer for the given (but unqualified) type.
            printer = lookup_type(type, val)
            if printer:
                return printer

            # If it's a typedef, look for a printer for the aliased type ...
            while type.code == gdb.TYPE_CODE_TYPEDEF:
                type = type.target()
                printer = lookup_type(type, val)
                if printer:
                    return printer

        # First, look for a printer for the given (but unqualified) type, or
        # its aliased type if it's a typedef.
        printer = lookup_type_or_alias(val.type, val)
        if printer:
            return printer

        # If it's a pointer, look for a printer for the pointed-to type.
        if val.type.code == gdb.TYPE_CODE_PTR:
            type = val.type.target()
            printer = lookup_type_or_alias(
                          type, val and val.dereference() or None)
            if printer:
                return printer

        # Cannot find a matching pretty printer in this collection.
        return None

class InferiorFunction:
    """A class whose instances are callable functions on the inferior
       process.
    """
    def __init__(self, function_name):
        self.function_name = function_name
        self.func = None

    def __call__(self, *args):
        if not self.func:
            self.func = gdb.parse_and_eval(self.function_name)
        return self.func(*args)

def children_as_map(children_iterator):
    """Convert an iteration of (key, value) pairs into the form required for
       a pretty-printer 'children' method when the display-hint is 'map'.
    """
    for k, v in children_iterator:
        yield 'key', k
        yield 'val', v


########################################################################

# Pretty-printing for APR library types.

# Some useful gdb.Type instances that can be initialized before any object
# files are loaded.
pvoidType = gdb.lookup_type('void').pointer()
cstringType = gdb.lookup_type('char').pointer()

# Some functions that resolve to calls into the inferior process.
apr_hash_count = InferiorFunction('apr_hash_count')
apr_hash_first = InferiorFunction('apr_hash_first')
apr_hash_next = InferiorFunction('apr_hash_next')
apr_hash_this_key = InferiorFunction('apr_hash_this_key')
apr_hash_this_val = InferiorFunction('apr_hash_this_val')

def children_of_apr_hash(hash_p, value_type=None):
    """Iterate over an 'apr_hash_t *' GDB value, in the way required for a
       pretty-printer 'children' method when the display-hint is 'map'.
       Cast the value pointers to VALUE_TYPE, or return values as '...' if
       VALUE_TYPE is None.
    """
    hi = apr_hash_first(0, hash_p)
    while (hi):
        k = apr_hash_this_key(hi).reinterpret_cast(cstringType)
        if value_type:
            val = apr_hash_this_val(hi).reinterpret_cast(value_type)
        else:
            val = '...'
        try:
            key = k.string()
        except:
            key = '<unreadable>'
        yield key, val
        hi = apr_hash_next(hi)

class AprHashPrinter:
    """for 'apr_hash_t' of 'char *' keys and unknown values"""
    def __init__(self, val):
        if val:
            self.hash_p = val.address
        else:
            self.hash_p = val

    def to_string(self):
        """Return a string to be displayed before children are displayed, or
           return None if we don't want any such.
        """
        if not self.hash_p:
            return 'NULL'
        return 'hash of ' + str(apr_hash_count(self.hash_p)) + ' items'

    def children(self):
        if not self.hash_p:
            return []
        return children_as_map(children_of_apr_hash(self.hash_p))

    def display_hint(self):
        return 'map'

def children_of_apr_array(array, value_type):
    """Iterate over an 'apr_array_header_t' GDB value, in the way required for
       a pretty-printer 'children' method when the display-hint is 'array'.
       Cast the values to VALUE_TYPE.
    """
    nelts = int(array['nelts'])
    elts = array['elts'].reinterpret_cast(value_type.pointer())
    for i in range(nelts):
        yield str(i), elts[i]

class AprArrayPrinter:
    """for 'apr_array_header_t' of unknown elements"""
    def __init__(self, val):
        self.array = val

    def to_string(self):
        if not self.array:
            return 'NULL'
        nelts = self.array['nelts']
        return 'array of ' + str(int(nelts)) + ' items'

    def children(self):
        # We can't display the children as we don't know their type.
        return []

    def display_hint(self):
        return 'array'

########################################################################

# Pretty-printing for Subversion libsvn_subr types.

class SvnBooleanPrinter:
    """for svn_boolean_t"""
    def __init__(self, val):
        self.val = val

    def to_string(self):
        if self.val is None:
            return '(NULL)'
        if self.val:
            return 'TRUE'
        else:
            return 'FALSE'

class SvnStringPrinter:
    """for svn_string_t"""
    def __init__(self, val):
        self.val = val

    def to_string(self):
        if not self.val:
            return 'NULL'

        data = self.val['data']
        len = int(self.val['len'])
        return data.string(length=len)

    def display_hint(self):
        if self.val:
            return 'string'

class SvnMergeRangePrinter:
    """for svn_merge_range_t"""
    def __init__(self, val):
        self.val = val

    def to_string(self):
        if not self.val:
            return 'NULL'

        r = self.val
        start = int(r['start'])
        end = int(r['end'])
        if start >= 0 and start < end:
            if start + 1 == end:
                rs = str(end)
            else:
                rs = str(start + 1) + '-' + str(end)
        elif end >= 0 and end < start:
            if start == end + 1:
                rs = '-' + str(start)
            else:
                rs = str(start) + '-' + str(end + 1)
        else:
            rs = '(INVALID: s=%d, e=%d)' % (start, end)
        if not r['inheritable']:
            rs += '*'
        return rs

    def display_hint(self):
        if self.val:
            return 'string'

class SvnRangelistPrinter:
    """for svn_rangelist_t"""
    def __init__(self, val):
        self.array = val
        self.svn_merge_range_t = gdb.lookup_type('svn_merge_range_t')

    def to_string(self):
        if not self.array:
            return 'NULL'

        s = ''
        for key, val in children_of_apr_array(self.array,
                                              self.svn_merge_range_t.pointer()):
            if s:
                s += ','
            s += SvnMergeRangePrinter(val).to_string()
        return s

    def display_hint(self):
        if self.array:
            return 'string'

class SvnMergeinfoPrinter:
    """for svn_mergeinfo_t"""
    def __init__(self, val):
        self.hash_p = val
        self.svn_rangelist_t = gdb.lookup_type('svn_rangelist_t')

    def to_string(self):
        if self.hash_p == 0:
            return 'NULL'

        s = ''
        for key, val in children_of_apr_hash(self.hash_p,
                                             self.svn_rangelist_t.pointer()):
            if s:
                s += '; '
            s += key + ':' + SvnRangelistPrinter(val).to_string()
        return '{ ' + s + ' }'

class SvnMergeinfoCatalogPrinter:
    """for svn_mergeinfo_catalog_t"""
    def __init__(self, val):
        self.hash_p = val
        self.svn_mergeinfo_t = gdb.lookup_type('svn_mergeinfo_t')

    def to_string(self):
        if self.hash_p == 0:
            return 'NULL'

        s = ''
        for key, val in children_of_apr_hash(self.hash_p,
                                             self.svn_mergeinfo_t):
            if s:
                s += ',\n  '
            s += "'" + key + "': " + SvnMergeinfoPrinter(val).to_string()
        return '{ ' + s + ' }'

########################################################################

# Pretty-printing for Subversion libsvn_client types.

class SvnPathrevPrinter:
    """for svn_client__pathrev_t"""
    def __init__(self, val):
        self.val = val

    def to_string(self):
        if not self.val:
            return 'NULL'

        rev = int(self.val['rev'])
        url = self.val['url'].string()
        repos_root_url = self.val['repos_root_url'].string()
        relpath = url[len(repos_root_url):]
        return "%s@%d" % (relpath, rev)

    def display_hint(self):
        if self.val:
            return 'string'


########################################################################

libapr_printer = None
libsvn_printer = None

def build_libsvn_printers():
    """Construct the pretty-printer objects."""

    global libapr_printer, libsvn_printer

    libapr_printer = TypedefRegexCollectionPrettyPrinter("libapr")
    libapr_printer.add_printer('apr_hash_t', r'^apr_hash_t$',
                               AprHashPrinter)
    libapr_printer.add_printer('apr_array_header_t', r'^apr_array_header_t$',
                               AprArrayPrinter)

    libsvn_printer = TypedefRegexCollectionPrettyPrinter("libsvn")
    libsvn_printer.add_printer('svn_boolean_t', r'^svn_boolean_t$',
                               SvnBooleanPrinter)
    libsvn_printer.add_printer('svn_string_t', r'^svn_string_t$',
                               SvnStringPrinter)
    libsvn_printer.add_printer('svn_client__pathrev_t', r'^svn_client__pathrev_t$',
                               SvnPathrevPrinter)
    libsvn_printer.add_printer('svn_merge_range_t', r'^svn_merge_range_t$',
                               SvnMergeRangePrinter)
    libsvn_printer.add_printer('svn_rangelist_t', r'^svn_rangelist_t$',
                               SvnRangelistPrinter)
    libsvn_printer.add_printer('svn_mergeinfo_t', r'^svn_mergeinfo_t$',
                               SvnMergeinfoPrinter)
    libsvn_printer.add_printer('svn_mergeinfo_catalog_t', r'^svn_mergeinfo_catalog_t$',
                               SvnMergeinfoCatalogPrinter)


def register_libsvn_printers(obj):
    """Register the pretty-printers for the object file OBJ."""

    global libapr_printer, libsvn_printer

    # Printers registered later take precedence.
    gdb.printing.register_pretty_printer(obj, libapr_printer)
    gdb.printing.register_pretty_printer(obj, libsvn_printer)


# Construct the pretty-printer objects, once, at GDB start-up time when this
# Python module is loaded.  (Registration happens later, once per object
# file.)
build_libsvn_printers()
