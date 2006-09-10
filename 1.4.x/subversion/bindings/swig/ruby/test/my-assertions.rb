require "test/unit"
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

      def assert_nested_sorted_array(expected, actual, message=nil)
        _wrap_assertion do
          assert_equal(expected.collect {|elem| elem.sort},
                       actual.collect {|elem| elem.sort},
                       message)
        end
      end
    end
  end
end
