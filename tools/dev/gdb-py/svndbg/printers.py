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
       any other type that doesn't have a tag name."""

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
       process."""
    def __init__(self, function_name):
        self.function_name = function_name
        self.func = None

    def __call__(self, *args):
        if not self.func:
            self.func = gdb.parse_and_eval(self.function_name)
        return self.func(*args)

def children_as_map(children_iterator):
    """Convert an iteration of (key, value) pairs into the form required for
       a pretty-printer 'children' method when the display-hint is 'map'."""
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
       pretty-printer 'children' method when the display-hint is 'array'.
       Cast the value pointers to VALUE_TYPE, or return values as '...' if
       VALUE_TYPE is None."""
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
        self.hash_p = val.address

    def to_string(self):
        """Return a string to be displayed before children are displayed, or
           return None if we don't want any such."""
        if not self.hash_p:
            return 'NULL'
        return 'hash of ' + str(apr_hash_count(self.hash_p)) + ' items'

    def children(self):
        if not self.hash_p:
            return []
        return children_as_map(children_of_apr_hash(self.hash_p))

    def display_hint(self):
        return 'map'

class PtrAprHashPrinter(AprHashPrinter):
    """for pointer to 'apr_hash_t' of 'char *' keys and unknown values"""
    def __init__(self, val):
        self.hash_p = val

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

class PtrAprArrayPrinter(AprArrayPrinter):
    """for pointer to 'apr_array_header_t' of unknown elements"""
    def __init__(self, val):
        if not val:
            self.array = None
        else:
            self.array = val.dereference()


########################################################################

# Pretty-printing for Subversion library types.

class SvnStringPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        # Make sure string * works, too
        val = self.val

        ptr = val['data']
        len = val['len']

        return "length: " + str(int(len)) + "; contents: '" + ptr.string(length=len) + "'"

    def display_hint(self):
        return 'string'

class SvnMergeinfoCatalogPrinter:
    """for svn_mergeinfo_catalog_t"""
    def __init__(self, val):
        self.hash_p = val

    def to_string(self):
        if self.hash_p == 0:
            return 'NULL'
        return 'mergeinfo catalog of ' + str(apr_hash_count(self.hash_p)) + ' items'

    def children(self):
        if self.hash_p == 0:
            # Return an empty list so GDB prints only the 'NULL' that is
            # returned by to_string().  If instead we were to return None
            # here, GDB would issue a 'not iterable' error message.
            return []
        mergeinfoType = gdb.lookup_type('svn_mergeinfo_t')
        return children_as_map(children_of_apr_hash(self.hash_p, mergeinfoType))

    def display_hint(self):
        return 'map'


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
    #       the collection above can't match them, but ideally we'd fix that
    #       matching and move these entries to there so that they get used
    #       for any typedef that doesn't have its own specific pretty-printer
    #       registered.
    libapr_printer2 = TypedefRegexCollectionPrettyPrinter("libapr2")
    libapr_printer2.add_printer('apr_hash_t *', r'^apr_hash_t \*$',
                                PtrAprHashPrinter)
    libapr_printer2.add_printer('apr_array_header_t *', r'^apr_array_header_t \*$',
                                PtrAprArrayPrinter)

    # These sub-printers match a struct's (or union)'s tag name,
    # after stripping typedefs, references and const/volatile qualifiers.
    libsvn_printer = RegexpCollectionPrettyPrinter("libsvn")
    libsvn_printer.add_printer('svn_string_t', r'^svn_string_t$',
                               SvnStringPrinter)

    # These sub-printers match a type name at the point of use,
    # after stripping const/volatile qualifiers.
    libsvn_printer2 = TypedefRegexCollectionPrettyPrinter("libsvn2")
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
