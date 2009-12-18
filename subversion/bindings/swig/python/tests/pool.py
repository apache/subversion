import unittest, weakref, setup_path
import svn.core, svn.client, libsvn.core
from svn.core import *
from libsvn.core import application_pool, GenericSWIGWrapper

# Test case for the new automatic pool management infrastructure

class PoolTestCase(unittest.TestCase):

  def assertNotNone(self, value):
    """Assert that the specified value is not None"""
    return self.assertNotEqual(value, None);

  def assertNone(self, value):
    """Assert that the specified value is None"""
    return self.assertEqual(value, None);

  def test_object_struct_members(self):
    """Check that object struct members work correctly"""

    # Test good object assignment operations
    client_ctx = svn.client.svn_client_create_context()
    config = svn.core.svn_config_get_config(None)
    client_ctx.config = config

    # Check that parent pools are set correctly on struct accesses
    self.assertEqual(client_ctx.config._parent_pool, config._parent_pool)

    # Test bad object assignment operations
    def test_bad_assignment(self):
      head_revision = svn.core.svn_opt_revision_t()
      head_revision.kind = config
    self.assertRaises(TypeError, test_bad_assignment)

  def test_assert_valid(self):
    """Test assert_valid method on proxy objects"""

    # Test assert_valid with destroy()
    client_ctx = svn.client.svn_client_create_context()
    config = svn.core.svn_config_get_config(None)
    wrapped_config = GenericSWIGWrapper(config, config._parent_pool)
    client_ctx.config = config
    config.assert_valid()
    wrapped_config.assert_valid()
    client_ctx.config.assert_valid()
    config._parent_pool.destroy()
    self.assertRaises(AssertionError, lambda: config.assert_valid())
    self.assertRaises(AssertionError, lambda: wrapped_config.assert_valid())
    self.assertRaises(AssertionError, lambda: client_ctx.config)

    # Test assert_valid with clear()
    client_ctx = svn.client.svn_client_create_context()
    config = svn.core.svn_config_get_config(None)
    wrapped_config = GenericSWIGWrapper(config, config._parent_pool)
    client_ctx.config = config
    config.assert_valid()
    client_ctx.config.assert_valid()
    wrapped_config.assert_valid()
    config._parent_pool.clear()
    self.assertRaises(AssertionError, lambda: config.assert_valid())
    self.assertRaises(AssertionError, lambda: wrapped_config.assert_valid())
    self.assertRaises(AssertionError, lambda: client_ctx.config)

  def test_integer_struct_members(self):
    """Check that integer struct members work correctly"""

    # Test good integer assignment operations
    rev = svn.core.svn_opt_revision_t()
    rev.kind = svn.core.svn_opt_revision_number
    rev.value.number = 10
    self.assertEqual(rev.kind, svn.core.svn_opt_revision_number)
    self.assertEqual(rev.value.number, 10)

    # Test bad integer assignment operations
    def test_bad_assignment(self):
      client_ctx = svn.client.svn_client_create_context()
      client_ctx.config = 2
    self.assertRaises(TypeError, test_bad_assignment)

  def test_pool(self):
    # Create pools
    parent_pool = Pool()
    parent_pool_ref = weakref.ref(parent_pool)
    pool = Pool(Pool(parent_pool))
    pool = Pool(pool)

    # Make sure proper exceptions are raised with incorrect input
    self.assertRaises(TypeError, lambda: Pool("abcd"));

    # Check that garbage collection is working OK
    self.assertNotNone(parent_pool_ref())
    top_pool_ref = weakref.ref(parent_pool._parent_pool)
    del parent_pool
    self.assertNotNone(parent_pool_ref())
    self.assertNotNone(top_pool_ref())
    pool.clear()
    newpool = libsvn.core.svn_pool_create(pool)
    libsvn.core.apr_pool_destroy(newpool)
    self.assertNotNone(newpool)
    pool.clear()
    self.assertNotNone(parent_pool_ref())
    del pool
    self.assertNotNone(parent_pool_ref())
    del newpool
    self.assertNone(parent_pool_ref())
    self.assertNone(top_pool_ref())

    # Make sure anonymous pools are destroyed properly
    anonymous_pool_ref = weakref.ref(Pool())
    self.assertNone(anonymous_pool_ref())

  def test_compatibility_layer(self):
    # Create a new pool
    pool = Pool()
    parent_pool_ref = weakref.ref(pool)
    pool = svn_pool_create(Pool(pool))
    pool_ref = weakref.ref(pool)

    # Make sure proper exceptions are raised with incorrect input
    self.assertRaises(TypeError, lambda: svn_pool_create("abcd"));

    # Test whether pools are destroyed properly
    pool = svn_pool_create(pool)
    self.assertNotNone(pool_ref())
    self.assertNotNone(parent_pool_ref())
    del pool
    self.assertNone(pool_ref())
    self.assertNone(parent_pool_ref())

    # Ensure that AssertionErrors are raised when a pool is deleted twice
    newpool = Pool()
    newpool2 = Pool(newpool)
    svn_pool_clear(newpool)
    self.assertRaises(AssertionError, lambda: libsvn.core.apr_pool_destroy(newpool2))
    self.assertRaises(AssertionError, lambda: svn_pool_destroy(newpool2));
    svn_pool_destroy(newpool)
    self.assertRaises(AssertionError, lambda: svn_pool_destroy(newpool))

    # Try to allocate memory from a destroyed pool
    self.assertRaises(AssertionError, lambda: svn_pool_create(newpool))

    # Create and destroy a pool
    svn_pool_destroy(svn_pool_create())

    # Make sure anonymous pools are destroyed properly
    anonymous_pool_ref = weakref.ref(svn_pool_create())
    self.assertNone(anonymous_pool_ref())

    # Try to cause a segfault using apr_terminate
    apr_terminate()
    apr_initialize()
    apr_terminate()
    apr_terminate()

    # Destroy the application pool
    svn_pool_destroy(libsvn.core.application_pool)

    # Double check that the application pool has been deleted
    self.assertNone(libsvn.core.application_pool)

    # Try to allocate memory from the old application pool
    self.assertRaises(AssertionError, lambda: svn_pool_create(application_pool));

    # Bring the application pool back to life
    svn_pool_create()

    # Double check that the application pool has been created
    self.assertNotNone(libsvn.core.application_pool)

    # We can still destroy and create pools at will
    svn_pool_destroy(svn_pool_create())

def suite():
  return unittest.makeSuite(PoolTestCase, 'test')

if __name__ == '__main__':
  runner = unittest.TextTestRunner()
  runner.run(suite());
