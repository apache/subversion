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

  def test_version
    assert_equal(Svn::Core.subr_version, Svn::Wc.version)
  end

  def test_status
    file1 = "a"
    file1_path = File.join(@wc_path, file1)

    Svn::Wc::AdmAccess.open(nil, @wc_path, false, 0) do |adm|
      status = adm.status(file1_path)
      assert_equal(Svn::Wc::STATUS_NONE, status.text_status)
      assert_nil(status.entry)
    end

    non_exist_child_path = File.join(@wc_path, "NOT-EXIST")
    assert_nothing_raised do
      Svn::Wc::AdmAccess.probe_open(nil, non_exist_child_path, false, 0){}
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

  def test_wc
    assert_not_equal(0, Svn::Wc.check_wc(@wc_path))
    assert(Svn::Wc.normal_prop?("name"))
    assert(Svn::Wc.wc_prop?("#{Svn::Core::PROP_WC_PREFIX}name"))
    assert(Svn::Wc.entry_prop?("#{Svn::Core::PROP_ENTRY_PREFIX}name"))
  end

  def test_adm_access
    log = "sample log"
    source = "sample source"
    file = "file"
    prop_name = "name"
    prop_value = "value"
    path = File.join(@wc_path, file)
    ctx = make_context(log)

    File.open(path, "w") {|f| f.print(source)}
    ctx.add(path)
    rev = ctx.ci(@wc_path).revision

    result = Svn::Wc::AdmAccess.open_anchor(path, true, 5)
    anchor_access, target_access, target = result
    
    assert_equal(file, target)
    assert_equal(@wc_path, anchor_access.path)
    assert_equal(@wc_path, target_access.path)
    
    assert(anchor_access.locked?)
    assert(target_access.locked?)
    
    assert(!target_access.has_binary_prop?(path))
    assert(!target_access.text_modified?(path))
    assert(!target_access.props_modified?(path))
    
    File.open(path, "w") {|f| f.print(source * 2)}
    target_access.set_prop(prop_name, prop_value, path)
    assert_equal(prop_value, target_access.prop(prop_name, path))
    assert_equal({prop_name => prop_value},
                 target_access.prop_list(path))
    assert(target_access.text_modified?(path))
    assert(target_access.props_modified?(path))
    
    target_access.set_prop("name", nil, path)
    assert(!target_access.props_modified?(path))
    
    target_access.revert(path)
    assert(!target_access.text_modified?(path))
    assert(!target_access.props_modified?(path))
    
    anchor_access.close
    target_access.close

    access = Svn::Wc::AdmAccess.probe_open(nil, path, false, 5)
    assert(!Svn::Wc.default_ignores({}).empty?)
    assert_equal(Svn::Wc.default_ignores({}), access.ignores({}))
    assert(access.wc_root?(@wc_path))
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
