require "my-assertions"
require "util"

require "svn/core"
require "svn/wc"

class SvnWcTest < Test::Unit::TestCase
  include SvnTestUtil

  def setup
    setup_basic
  end

  def teardown
    teardown_basic
  end

  def test_status
    file1 = "a"
    file1_path = File.join(@wc_path, file1)

    Svn::Wc::AdmAccess.open(nil, @wc_path, false, 0) do |adm|
      status = adm.status(file1_path)
      assert_equal(Svn::Wc::STATUS_NONE, status.text_status)
      assert_nil(status.entry)
    end
    
    FileUtils.touch(file1_path)
    Svn::Wc::AdmAccess.open(nil, @wc_path, false, 0) do |adm|
      status = adm.status(file1_path)
      assert_equal(Svn::Wc::STATUS_UNVERSIONED, status.text_status)
      assert_nil(status.entry)
    end
    
    log = "sample log"
    ctx = make_context(log)
    ctx.add(file1_path)
    Svn::Wc::AdmAccess.open(nil, @wc_path, false, 0) do |adm|
      status = adm.status(file1_path)
      assert_equal(Svn::Wc::STATUS_ADDED, status.text_status)
    end
    
    commit_info = ctx.commit(@wc_path)
    
    Svn::Wc::AdmAccess.open(nil, @wc_path, false, 0) do |adm|
      status = adm.status(file1_path)
      assert_equal(Svn::Wc::STATUS_NORMAL, status.text_status)
      assert_equal(commit_info.revision, status.entry.revision)
    end
  end
  
  def test_locked
    log = "sample log"

    assert(!Svn::Wc.locked?(@wc_path))
    ctx = make_context(log)
    assert_raise(Svn::Error::FS_NO_SUCH_REVISION) do
      ctx.update(@wc_path, youngest_rev + 1)
    end
    assert(Svn::Wc.locked?(@wc_path))
    ctx.cleanup(@wc_path)
    assert(!Svn::Wc.locked?(@wc_path))
  end
end
