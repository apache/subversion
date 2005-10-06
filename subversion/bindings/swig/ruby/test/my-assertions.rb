require "test/unit/assertions"

module Test
  module Unit
    module Assertions

      def assert_true(boolean, message=nil)
        _wrap_assertion do
          assert_equal(true, boolean, message)
        end
      end
      
      def assert_false(boolean, message=nil)
        _wrap_assertion do
          assert_equal(false, boolean, message)
        end
      end
      
    end
  end
end
