#!/home/djames/bin/python

from ctypes import *
from functions import *
from tempfile import TemporaryFile
from csvn.ext.listmixin import ListMixin
import sys

# Convert the standard files
stdout = PyFile_AsFile(sys.stdout)
stderr = PyFile_AsFile(sys.stderr)
stdin = PyFile_AsFile(sys.stdin)

def svn_pool_create(pool):
    return svn_pool_create_ex(pool, NULL)

def svn_pool_destroy(pool):
    return apr_pool_destroy(pool)

def svn_pool_clear(pool):
    return apr_pool_clear(pool)

# Initialize everything
svn_cmdline_init("", stderr)

application_pool = None

def _mark_weakpool_invalid(weakpool):
  if weakpool and weakpool() and hasattr(weakpool(), "_is_valid"):
    del weakpool()._is_valid

class Pool(object):
  def __init__(self, parent_pool=None):
    """Create a new memory pool"""
    self._parent_pool = parent_pool or application_pool
    self._as_parameter_ = svn_pool_create(self._parent_pool)
    self._mark_valid()

    # Protect important functions from GC
    self._svn_pool_destroy = apr_pool_destroy

  def valid(self):
    """Check whether this memory pool and its parents
    are still valid"""
    return hasattr(self,"_is_valid")

  def assert_valid(self):
    """Assert that this memory_pool is still valid."""
    assert self.valid(), "This pool has already been destroyed"

  def clear(self):
    """Clear embedded memory pool. Invalidate all subpools."""
    pool = self._parent_pool
    svn_pool_clear(self)
    self._mark_valid()

  def destroy(self):
    """Destroy embedded memory pool. If you do not destroy
    the memory pool manually, Python will destroy it
    automatically."""

    self.assert_valid()

    is_application_pool = not self._parent_pool

    # Destroy pool
    self._svn_pool_destroy(self)

    # Clear application pool if necessary
    if is_application_pool:
      global application_pool
      application_pool = None

    # Mark self as invalid
    if hasattr(self, "_parent_pool"):
      del self._parent_pool
    if hasattr(self, "_is_valid"):
      del self._is_valid

  def __del__(self):
    """Automatically destroy memory pools, if necessary"""
    if self.valid():
      self.destroy()

  def _mark_valid(self):
    """Mark pool as valid"""

    self._weakparent = None

    if self._parent_pool:
      import weakref

      # Make sure that the parent object is valid
      self._parent_pool.assert_valid()

      # Refer to self using a weakrefrence so that we don't
      # create a reference cycle
      weakself = weakref.ref(self)

      # Set up callbacks to mark pool as invalid when parents
      # are destroyed
      weakparent = weakref.ref(self._parent_pool._is_valid,
        lambda x: _mark_weakpool_invalid(weakself))

    # Mark pool as valid
    self._is_valid = lambda: 1
application_pool = Pool()

class Array(ListMixin):

    def __init__(self, type, items=None, size=0):
        self.type = type
        self.pool = Pool()
        if not items:
            self.header = apr_array_make(self.pool, size, sizeof(type))
        elif isinstance(items, POINTER(apr_array_header_t)):
            self.header = items
        elif isinstance(items, Array):
            self.header = apr_array_copy(self.pool, items)
        else:
            self.header = apr_array_make(self.pool, len(items),
                                         sizeof(type))
            self.extend(items)

    _as_parameter_ = property(fget=lambda self: self.header)
    elts = property(fget=lambda self: cast(self.header[0].elts, POINTER(self.type)))

    def _get_element(self, i):
        return self.elts[i]

    def _set_element(self, i, value):
        self.elts[i] = value

    def __len__(self):
        return self.header[0].nelts

    def _resize_region(self, start, end, new_size):
        diff = start-end+new_size

        # Growing
        if diff > 0:
            l = len(self)

            # Make space for the new items
            for i in xrange(diff):
                apr_array_push(self)

            # Move the old items out of the way, if necessary
            if end < l:
                src_idx = max(end-diff,0)
                memmove(byref(self.elts + end),
                        byref(self.elts[src_idx]),
                        (l-src_idx)*self.header[0].elt_size)

        # Shrinking
        elif diff < 0:

            # Overwrite the deleted items with items we still need
            if end < len(self):
                memmove(byref(self.elts[end+diff]),
                        byref(self.elts[end]),
                        (len(self)-end)*self.header[0].elt_size)

            # Shrink the array
            for i in xrange(-diff):
                apr_array_pop(self)


