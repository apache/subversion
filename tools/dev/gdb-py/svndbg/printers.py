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
    """Class for implementing a collection of regular-expression based
       pretty-printers, matching on the type name at the point of use, such
       as (but not necessarily) a 'typedef' name, ignoring 'const' or
       'volatile' qualifiers.

       This is modeled on RegexpCollectionPrettyPrinter, which (in GDB 7.3)
       matches on the base type's tag name and can't match a pointer type or
       any other type that doesn't have a tag name.
    """

    def __init__(self, name):
        super(TypedefRegexCollectionPrettyPrinter, self).__init__(name)

    def __call__(self, val):
        """Lookup the pretty-printer for the provided value."""

        # Get the type name, without 'const' or 'volatile' qualifiers.
        typename = str(val.type.unqualified())
        if not typename:
            return None

        # Iterate over table of type regexps to find an enabled printer for
        # that type.  Return an instantiation of the printer if found.
        for printer in self.subprinters:
            if printer.enabled and printer.compiled_re.search(typename):
                return printer.gen_printer(val)

        # Cannot find a pretty printer.  Return None.
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
svn__apr_hash_index_key = InferiorFunction('svn__apr_hash_index_key')
svn__apr_hash_index_val = InferiorFunction('svn__apr_hash_index_val')

def children_of_apr_hash(hash_p, value_type=None):
    """Iterate over an 'apr_hash_t *' GDB value, in the way required for a
       pretty-printer 'children' method when the display-hint is 'map'.
       Cast the value pointers to VALUE_TYPE, or return values as '...' if
       VALUE_TYPE is None.
    """
    hi = apr_hash_first(0, hash_p)
    while (hi):
        k = svn__apr_hash_index_key(hi).reinterpret_cast(cstringType)
        if value_type:
            val = svn__apr_hash_index_val(hi).reinterpret_cast(value_type)
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
        if val.type.code == gdb.TYPE_CODE_PTR:
            self.hash_p = val
        else:
            self.hash_p = val.address

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
       Cast the value pointers to VALUE_TYPE.
    """
    nelts = int(array['nelts'])
    elts = array['elts'].reinterpret_cast(value_type.pointer())
    for i in range(nelts):
        yield str(i), elts[i]

class AprArrayPrinter:
    """for 'apr_array_header_t' of unknown elements"""
    def __init__(self, val):
        if val.type.code == gdb.TYPE_CODE_PTR and val:
            self.array = val.dereference()
        else:
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

class SvnStringPrinter:
    """for svn_string_t"""
    def __init__(self, val):
        if val.type.code == gdb.TYPE_CODE_PTR and val:
            self.val = val.dereference()
        else:
            self.val = val

    def to_string(self):
        if not self.val:
            return 'NULL'

        data = self.val['data']
        len = int(self.val['len'])
        return data.string(length=len)

    def display_hint(self):
        return 'string'

class SvnMergeRangePrinter:
    """for svn_merge_range_t"""
    def __init__(self, val):
        if val.type.code == gdb.TYPE_CODE_PTR and val:
            self.val = val.dereference()
        else:
            self.val = val

    def to_string(self):
        if not self.val:
            return 'NULL'

        r = self.val
        rs = str(r['start']) + '-' + str(r['end'])
        if not r['inheritable']:
          rs += '*'
        return rs

    def display_hint(self):
        return 'string'

class SvnRangelistPrinter:
    """for svn_rangelist_t"""
    def __init__(self, val):
        if val.type.code == gdb.TYPE_CODE_PTR and val:
            self.array = val.dereference()
        else:
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
        rev = int(self.val['rev'])
        url = self.val['url'].string()
        repos_root_url = self.val['repos_root_url'].string()
        relpath = url[len(repos_root_url):]
        return "%s@%d" % (relpath, rev)

    def display_hint(self):
        return 'string'


########################################################################

libapr_printer = None
libapr_printer2 = None
libsvn_printer = None
libsvn_printer2 = None

def build_libsvn_printers():
    """Construct the pretty-printer objects."""

    global libapr_printer, libapr_printer2, libsvn_printer, libsvn_printer2

    # These sub-printers match a struct's (or union)'s tag name,
    # after stripping typedefs, references and const/volatile qualifiers.
    libapr_printer = RegexpCollectionPrettyPrinter("libapr")
    libapr_printer.add_printer('apr_hash_t', r'^apr_hash_t$',
                               AprHashPrinter)
    libapr_printer.add_printer('apr_array_header_t', r'^apr_array_header_t$',
                               AprArrayPrinter)

    # These sub-printers match a type name at the point of use,
    # after stripping const/volatile qualifiers.
    #
    # TODO: The "apr_foo_t *" entries are in this collection merely because
    #       the collection above can't match any pointer type (because the
    #       pointer itself has no tag-name).  Ideally we'd improve that
    #       matching so that for example the 'apr_hash_t *' entry would
    #       match both
    #         any typedef that resolves to pointer-to-apr_hash_t
    #       and
    #         pointer to any typedef that resolves to apr_hash_t
    #       for any typedef that doesn't have its own specific pretty-printer
    #       registered.
    libapr_printer2 = TypedefRegexCollectionPrettyPrinter("libapr2")
    libapr_printer2.add_printer('apr_hash_t *', r'^apr_hash_t \*$',
                                AprHashPrinter)
    libapr_printer2.add_printer('apr_array_header_t *', r'^apr_array_header_t \*$',
                                AprArrayPrinter)

    # These sub-printers match a struct's (or union)'s tag name,
    # after stripping typedefs, references and const/volatile qualifiers.
    libsvn_printer = RegexpCollectionPrettyPrinter("libsvn")
    libsvn_printer.add_printer('svn_string_t', r'^svn_string_t$',
                               SvnStringPrinter)

    # These sub-printers match a type name at the point of use,
    # after stripping const/volatile qualifiers.
    libsvn_printer2 = TypedefRegexCollectionPrettyPrinter("libsvn2")
    libsvn_printer2.add_printer('svn_string_t *', r'^svn_string_t \*$',
                               SvnStringPrinter)
    libsvn_printer2.add_printer('svn_client__pathrev_t', r'^svn_client__pathrev_t$',
                                SvnPathrevPrinter)
    libsvn_printer2.add_printer('svn_merge_range_t', r'^svn_merge_range_t$',
                                SvnMergeRangePrinter)
    libsvn_printer2.add_printer('svn_merge_range_t *', r'^svn_merge_range_t \*$',
                                SvnMergeRangePrinter)
    libsvn_printer2.add_printer('svn_rangelist_t', r'^svn_rangelist_t$',
                                SvnRangelistPrinter)
    libsvn_printer2.add_printer('svn_rangelist_t *', r'^svn_rangelist_t \*$',
                                SvnRangelistPrinter)
    libsvn_printer2.add_printer('svn_mergeinfo_t', r'^svn_mergeinfo_t$',
                                SvnMergeinfoPrinter)
    libsvn_printer2.add_printer('svn_mergeinfo_catalog_t', r'^svn_mergeinfo_catalog_t$',
                                SvnMergeinfoCatalogPrinter)


def register_libsvn_printers(obj):
    """Register the pretty-printers for the object file OBJ."""

    global libapr_printer, libapr_printer2, libsvn_printer, libsvn_printer2

    # Printers registered later take precedence.
    gdb.printing.register_pretty_printer(obj, libapr_printer)
    gdb.printing.register_pretty_printer(obj, libapr_printer2)
    gdb.printing.register_pretty_printer(obj, libsvn_printer)
    gdb.printing.register_pretty_printer(obj, libsvn_printer2)


# Construct the pretty-printer objects, once, at GDB start-up time when this
# Python module is loaded.  (Registration happens later, once per object
# file.)
build_libsvn_printers()
