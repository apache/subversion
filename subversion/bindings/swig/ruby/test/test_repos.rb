require "my-assertions"
require "util"

require "svn/core"
require "svn/fs"
require "svn/repos"
require "svn/client"

class TestSvnRepos < Test::Unit::TestCase
  include SvnTestUtil
  
  def setup
    setup_basic
  end

  def teardown
    teardown_basic
  end

  def test_path
    assert_equal(@repos_path, @repos.path)

    assert_equal(File.join(@repos_path, "db"), @repos.db_env)

    assert_equal(File.join(@repos_path, "conf"), @repos.conf_dir)
    
    assert_equal(File.join(@repos_path, "conf", "svnserve.conf"),
                 @repos.svnserve_conf)
    
    assert_equal(File.join(@repos_path, "locks"), @repos.lock_dir)


    hooks_dir = File.join(@repos_path, "hooks")
    assert_equal(File.join(hooks_dir, "start-commit"),
                 @repos.start_commit_hook)
    assert_equal(File.join(hooks_dir, "pre-commit"),
                 @repos.pre_commit_hook)
    assert_equal(File.join(hooks_dir, "post-commit"),
                 @repos.post_commit_hook)
    
    assert_equal(File.join(hooks_dir, "pre-revprop-change"),
                 @repos.pre_revprop_change_hook)
    assert_equal(File.join(hooks_dir, "post-revprop-change"),
                 @repos.post_revprop_change_hook)

    
    search_path = @repos_path
    assert_equal(@repos_path, Svn::Repos.find_root_path(search_path, @pool))
    search_path = "#{@repos_path}/XXX"
    assert_equal(@repos_path, Svn::Repos.find_root_path(search_path, @pool))

    search_path = "not-found"
    assert_equal(nil, Svn::Repos.find_root_path(search_path, @pool))
  end

  def test_transaction
    log = "sample log"
    ctx = make_context(log)
    ctx.checkout(@repos_uri, @wc_path)
    ctx.mkdir(["#{@wc_path}/new_dir"])
    
    prev_rev = @repos.youngest_rev
    past_date = Time.now
    @repos.transaction_for_commit(@author, log) do |txn|
      txn.abort
    end
    assert_equal(prev_rev, @repos.youngest_rev)
    assert_equal(prev_rev, @repos.dated_revision(past_date))
    
    prev_rev = @repos.youngest_rev
    @repos.transaction_for_commit(@author, log) do |txn|
    end
    assert_equal(prev_rev + 1, @repos.youngest_rev)
    assert_equal(prev_rev, @repos.dated_revision(past_date))
    assert_equal(prev_rev + 1, @repos.dated_revision(Time.now))
  end
  
end
