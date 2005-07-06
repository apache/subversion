require "my-assertions"
require "util"

require "svn/core"

class TestSvnCore < Test::Unit::TestCase
  include SvnTestUtil
  
  def setup
    @repos_path = "test/repos"
    setup_repository(@repos_path)
  end

  def teardown
    teardown_repository(@repos_path)
  end
  
  def test_binary_mime_type?
    assert(Svn::Core.binary_mime_type?("image/png"))
    assert(!Svn::Core.binary_mime_type?("text/plain"))
  end

  def test_new_pool
    
  end

  def test_not_new_auth_provider_object
    assert_raise(NoMethodError) do
      Svn::Core::AuthProviderObject.new
    end
  end
end
