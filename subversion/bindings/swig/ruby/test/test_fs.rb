require "my-assertions"
require "util"

require "svn/core"
require "svn/fs"
require "svn/repos"
require "svn/client"

class TestSvnFs < Test::Unit::TestCase
  include SvnTestUtil
  
  def setup
    setup_basic
  end

  def teardown
    teardown_basic
  end
  
  def test_prop
    log = "sample log"
    ctx = make_context(log)
    ctx.checkout(@repos_uri, @wc_path)
    ctx.mkdir(["#{@wc_path}/new_dir"])
    past_time = Time.parse(Time.new.to_s)
    info = ctx.commit([@wc_path])

    assert_equal(@author, info.author)
    assert_equal(@fs.youngest_rev, info.revision)
    assert(past_time <= info.date)
    assert(info.date <= Time.now)

    assert_equal(@author, prop(Svn::Core::PROP_REVISION_AUTHOR))
    assert_equal(log, prop(Svn::Core::PROP_REVISION_LOG))
  end

end
