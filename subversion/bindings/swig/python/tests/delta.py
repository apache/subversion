import unittest, setup_path
import svn.delta
import svn.core
from sys import version_info # For Python version check
if version_info[0] >= 3:
  # Python >=3.0
  from io import StringIO
else:
  # Python <3.0
  from cStringIO import StringIO

# Test case for svn.delta
class DeltaTestCase(unittest.TestCase):

  def testTxWindowHandler(self):
    """Test tx_invoke_window_handler"""
    src_stream = StringIO("hello world");
    target_stream = StringIO("bye world");

    # Invoke the window_handler using a helper function
    window_handler, baton = \
       svn.delta.tx_apply(src_stream, target_stream, None)
    svn.delta.tx_invoke_window_handler(window_handler, None, baton)

    # Invoke the window_handler directly (easier!)
    window_handler, baton = \
       svn.delta.tx_apply(src_stream, target_stream, None)
    window_handler(None, baton)

def suite():
  return unittest.makeSuite(DeltaTestCase, 'test')

if __name__ == '__main__':
  runner = unittest.TextTestRunner()
  runner.run(suite());
