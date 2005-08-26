require "my-assertions"

require "svn/core"
require "svn/util"

class SvnUtilTest < Test::Unit::TestCase

  def test_to_ruby_const_name
    assert_equal("ABC", Svn::Util.to_ruby_const_name("abc"))
    assert_equal("ABC_DEF", Svn::Util.to_ruby_const_name("abc_def"))
  end
  
  def test_to_ruby_class_name
    assert_equal("Abc", Svn::Util.to_ruby_class_name("abc"))
    assert_equal("AbcDef", Svn::Util.to_ruby_class_name("abc_def"))
  end

  def test_time
    now = Time.now.gmtime
    str = now.strftime("%Y-%m-%dT%H:%M:%S.") + "#{now.usec}Z"

    assert_equal(Time.at(now.to_i, 0), Svn::Util.string_to_time(str))
    Svn::Core::Pool.new do |pool|
      assert_equal(now, Svn::Util.string_to_time(str, pool))
    end

    apr_time = now.to_i * 1000000 + now.usec
    assert_equal(apr_time, Svn::Util.to_apr_time(now))
    assert_equal(apr_time, Svn::Util.to_apr_time(apr_time))
  end

  def test_set_pool
    obj = Object.new
    class << obj
      attr_accessor :pool
    end
    
    obj.pool = nil
    assert_nil(obj.pool)

    Svn::Core::Pool.new do |pool|
      Svn::Util.set_pool(pool) do
        obj
      end
      assert_equal(pool, obj.pool)
    end
  end
end
