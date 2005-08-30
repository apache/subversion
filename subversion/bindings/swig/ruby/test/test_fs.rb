require "my-assertions"
require "util"
require "time"

require "svn/core"
require "svn/fs"
require "svn/repos"
require "svn/client"

class SvnFsTest < Test::Unit::TestCase
  include SvnTestUtil
  
  def setup
    setup_basic
  end

  def teardown
    teardown_basic
  end

  def test_version
    assert_equal(Svn::Core.subr_version, Svn::Fs.version)
  end

  def test_create
    path = File.join(@tmp_path, "fs")
    config = {}

    assert(!File.exist?(path))
    Svn::Fs::FileSystem.create(path, config)
    assert(File.exist?(path))
    fs = Svn::Fs::FileSystem.open(path, config)
    assert_equal(path, fs.path)
    Svn::Fs::FileSystem.delete(path)
    assert(!File.exist?(path))
  end

  def test_hotcopy
    log = "sample log"
    file = "hello.txt"
    path = File.join(@wc_path, file)
    FileUtils.touch(path)
    
    ctx = make_context(log)
    ctx.add(path)
    commit_info = ctx.commit(@wc_path)
    rev = commit_info.revision
    
    assert_equal(log, ctx.log_message(path, rev))
    
    dest_path = File.join(@tmp_path, "dest")
    backup_path = File.join(@tmp_path, "back")
    config = {}

    dest_fs = Svn::Fs::FileSystem.create(dest_path, config)

    FileUtils.mv(@fs.path, backup_path)
    FileUtils.mv(dest_fs.path, @fs.path)

    assert_raises(Svn::Error::FS_NO_SUCH_REVISION) do
      assert_equal(log, ctx.log_message(path, rev))
    end

    Svn::Fs::FileSystem.hotcopy(backup_path, @fs.path)
    assert_equal(log, ctx.log_message(path, rev))
  end

  def test_access
    new_access = Svn::Fs::Access.new(@author)
    @fs.access = new_access
    assert_equal(new_access.username, @fs.access.username)
    @fs.access.add_lock_token("token")
  end

  def test_root
    log = "sample log"
    file = "sample.txt"
    src = "sample source"
    path_in_repos = "/#{file}"
    path = File.join(@wc_path, file)
    
    ctx = make_context(log)
    FileUtils.touch(path)
    ctx.add(path)
    rev1 = ctx.commit(@wc_path).revision
    file_id1 = @fs.root.node_id(path_in_repos)

    assert_equal([path_in_repos], @fs.root.paths_changed.keys)
    info = @fs.root.paths_changed[path_in_repos]
    assert(info.text_mod?)
    assert(info.add?)
    
    File.open(path, "w") {|f| f.print(src)}
    rev2 = ctx.commit(@wc_path).revision
    file_id2 = @fs.root.node_id(path_in_repos)

    assert_equal([path_in_repos], @fs.root.paths_changed.keys)
    info = @fs.root.paths_changed[path_in_repos]
    assert(info.text_mod?)
    assert(info.modify?)

    assert_equal([path_in_repos, rev2],
                 @fs.root.node_history(file).location)
    assert_equal([path_in_repos, rev2],
                 @fs.root.node_history(file).prev.location)
    assert_equal([path_in_repos, rev1],
                 @fs.root.node_history(file).prev.prev.location)

    assert(!@fs.root.dir?(path_in_repos))
    assert(@fs.root.file?(path_in_repos))

    assert(file_id1.related?(file_id2))
    assert_equal(1, file_id1.compare(file_id2))
    assert_equal(1, file_id2.compare(file_id1))
    
    assert_equal(rev2, @fs.root.node_created_rev(path_in_repos))
    assert_equal(path_in_repos, @fs.root.node_created_path(path_in_repos))
  end

  def test_prop
    log = "sample log"
    ctx = make_context(log)
    ctx.checkout(@repos_uri, @wc_path)
    ctx.mkdir(["#{@wc_path}/new_dir"])
    past_time = Time.parse(Time.new.iso8601)
    info = ctx.commit([@wc_path])

    assert_equal(@author, info.author)
    assert_equal(@fs.youngest_rev, info.revision)
    assert(past_time <= info.date)
    assert(info.date <= Time.now)

    assert_equal(@author, prop(Svn::Core::PROP_REVISION_AUTHOR))
    assert_equal(log, prop(Svn::Core::PROP_REVISION_LOG))
  end

end
