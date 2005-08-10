require "my-assertions"
require "util"

require "svn/core"
require "svn/client"

class SvnClientTest < Test::Unit::TestCase
  include SvnTestUtil

  def setup
    setup_basic
  end

  def teardown
    teardown_basic
  end

  def test_version
    assert_equal(Svn::Core.subr_version, Svn::Client.version)
  end

  def test_add_not_recurse
    log = "sample log"
    file = "hello.txt"
    src = "Hello"
    dir = "dir"
    dir_path = File.join(@wc_path, dir)
    path = File.join(dir_path, file)
    uri = "#{@repos_uri}/#{dir}/#{file}"

    ctx = make_context(log)
    FileUtils.mkdir(dir_path)
    File.open(path, "w") {|f| f.print(src)}
    ctx.add(dir_path, false)
    ctx.commit(@wc_path)

    assert_raise(Svn::Error::FS_NOT_FOUND) do
      ctx.cat(uri)
    end
  end

  def test_add_recurse
    log = "sample log"
    file = "hello.txt"
    src = "Hello"
    dir = "dir"
    dir_path = File.join(@wc_path, dir)
    path = File.join(dir_path, file)
    uri = "#{@repos_uri}/#{dir}/#{file}"

    ctx = make_context(log)
    FileUtils.mkdir(dir_path)
    File.open(path, "w") {|f| f.print(src)}
    ctx.add(dir_path)
    ctx.commit(@wc_path)

    assert_equal(src, ctx.cat(uri))
  end

  def test_add_force
    log = "sample log"
    file = "hello.txt"
    src = "Hello"
    dir = "dir"
    dir_path = File.join(@wc_path, dir)
    path = File.join(dir_path, file)
    uri = "#{@repos_uri}/#{dir}/#{file}"

    ctx = make_context(log)
    FileUtils.mkdir(dir_path)
    File.open(path, "w") {|f| f.print(src)}
    ctx.add(dir_path, false)
    ctx.commit(@wc_path)

    assert_raise(Svn::Error::ENTRY_EXISTS) do
      ctx.add(dir_path, true, false)
    end
    
    ctx.add(dir_path, true, true)
    ctx.commit(@wc_path)
    assert_equal(src, ctx.cat(uri))
  end

  def test_add_no_ignore
    log = "sample log"
    file = "hello.txt"
    src = "Hello"
    dir = "dir"
    dir_path = File.join(@wc_path, dir)
    path = File.join(dir_path, file)
    uri = "#{@repos_uri}/#{dir}/#{file}"

    ctx = make_context(log)
    FileUtils.mkdir(dir_path)
    File.open(path, "w") {|f| f.print(src)}
    ctx.add(dir_path, false)
    ctx.propset(Svn::Core::PROP_IGNORE, file, dir_path)
    ctx.commit(@wc_path)

    ctx.add(dir_path, true, true, false)
    ctx.commit(@wc_path)
    assert_raise(Svn::Error::FS_NOT_FOUND) do
      ctx.cat(uri)
    end
    
    ctx.add(dir_path, true, true, true)
    ctx.commit(@wc_path)
    assert_equal(src, ctx.cat(uri))
  end

  def test_mkdir
    log = "sample log"
    dir = "dir"
    deep_dir = ["d", "e", "e", "p"]
    dir2 = "dir2"
    dir_uri = "#{@repos_uri}/#{dir}"
    deep_dir_uri = "#{@repos_uri}/#{deep_dir.join('/')}"
    dir2_uri = "#{@repos_uri}/#{dir2}"
    dir_path = File.join(@wc_path, dir)
    deep_dir_path = File.join(@wc_path, *deep_dir)
    dir2_path = File.join(@wc_path, dir2)

    ctx = make_context(log)

    assert(!File.exist?(dir_path))
    ctx.mkdir(dir_path)
    assert(File.exist?(dir_path))
    assert_raises(Svn::Error::ENTRY_EXISTS) do
      ctx.add(dir_path)
    end
    old_rev = ctx.commit(@wc_path).revision

    new_rev = ctx.mkdir(dir2_uri).revision
    assert_equal(old_rev + 1, new_rev)
    assert_raises(Svn::Error::FS_ALREADY_EXISTS) do
      ctx.mkdir(dir2_uri)
    end
    assert(!File.exist?(dir2_path))
    ctx.update(@wc_path)
    assert(File.exist?(dir2_path))

    assert_raises(Svn::Error) do
      ctx.mkdir(deep_dir_path)
    end
  end

  def test_mkdir_multiple
    log = "sample log"
    dir = "dir"
    dir2 = "dir2"
    dirs = [dir, dir2]
    dirs_path = dirs.collect{|d| File.join(@wc_path, d)}
    dirs_uri = dirs.collect{|d| "#{@repos_uri}/#{d}"}

    ctx = make_context(log)

    notify_info = []
    ctx.set_notify_func2 do |notify|
      notify_info << [notify.path, notify.action]
    end
    
    dirs_path.each do |path|
      assert(!File.exist?(path))
    end
    ctx.mkdir(dirs_path)
    assert_equal(dirs_path.collect do |d|
                   [d, Svn::Wc::NOTIFY_STATE_INAPPLICABLE]
                 end,
                 notify_info)
    dirs_path.each do |path|
      assert(File.exist?(path))
    end

    notify_info.clear
    ctx.commit(@wc_path)
    assert_equal(dirs_path.collect do |d|
                   [d, Svn::Wc::NOTIFY_COMMIT_ADDED]
                 end,
                 notify_info)
  end

  def test_mkdir_multiple2
    log = "sample log"
    dir = "dir"
    dir2 = "dir2"
    dirs = [dir, dir2]
    dirs_path = dirs.collect{|d| File.join(@wc_path, d)}
    dirs_uri = dirs.collect{|d| "#{@repos_uri}/#{d}"}

    ctx = make_context(log)

    notify_info = []
    ctx.set_notify_func2 do |notify|
      notify_info << [notify.path, notify.action]
    end
    
    dirs_path.each do |path|
      assert(!File.exist?(path))
    end
    ctx.mkdir(*dirs_path)
    assert_equal(dirs_path.collect do |d|
                   [d, Svn::Wc::NOTIFY_STATE_INAPPLICABLE]
                 end,
                 notify_info)
    dirs_path.each do |path|
      assert(File.exist?(path))
    end

    notify_info.clear
    ctx.commit(@wc_path)
    assert_equal(dirs_path.collect do |d|
                   [d, Svn::Wc::NOTIFY_COMMIT_ADDED]
                 end,
                 notify_info)
  end

  def test_delete
    log = "sample log"
    src = "sample source\n"
    file = "file.txt"
    dir = "dir"
    path = File.join(@wc_path, file)
    dir_path = File.join(@wc_path, dir)

    ctx = make_context(log)

    File.open(path, "w") {|f| f.print(src)}
    ctx.add(path)
    ctx.mkdir(dir_path)
    ctx.commit(@wc_path)

    ctx.delete([path, dir_path])
    ctx.commit(@wc_path)
    assert(!File.exist?(path))
    assert(!File.exist?(dir_path))

    
    File.open(path, "w") {|f| f.print(src)}
    ctx.add(path)
    ctx.commit(@wc_path)

    File.open(path, "w") {|f| f.print(src * 2)}
    assert_raises(Svn::Error::CLIENT_MODIFIED) do
      ctx.delete(path)
    end
    assert_raises(Svn::Error::WC_LOCKED) do
      ctx.delete(path, true)
    end
    ctx.cleanup(@wc_path)
    ctx.delete(path, true)
    ctx.commit(@wc_path)
    assert(!File.exist?(path))
  end
 
  def test_delete_alias
    log = "sample log"
    src = "sample source\n"
    file = "file.txt"
    dir = "dir"
    path = File.join(@wc_path, file)
    dir_path = File.join(@wc_path, dir)

    ctx = make_context(log)

    File.open(path, "w") {|f| f.print(src)}
    ctx.add(path)
    ctx.mkdir(dir_path)
    ctx.commit(@wc_path)

    ctx.rm([path, dir_path])
    ctx.commit(@wc_path)
    assert(!File.exist?(path))
    assert(!File.exist?(dir_path))

    
    File.open(path, "w") {|f| f.print(src)}
    ctx.add(path)
    ctx.commit(@wc_path)

    File.open(path, "w") {|f| f.print(src * 2)}
    assert_raises(Svn::Error::CLIENT_MODIFIED) do
      ctx.rm(path)
    end
    assert_raises(Svn::Error::WC_LOCKED) do
      ctx.rm_f(path)
    end
    ctx.cleanup(@wc_path)
    ctx.rm_f(path)
    ctx.commit(@wc_path)
    assert(!File.exist?(path))

    File.open(path, "w") {|f| f.print(src)}
    ctx.add(path)
    ctx.mkdir(dir_path)
    ctx.commit(@wc_path)

    ctx.rm_f(path, dir_path)
    ctx.commit(@wc_path)
    assert(!File.exist?(path))
    assert(!File.exist?(dir_path))
  end
  
  def test_import
    src = "source\n"
    log = "sample log"
    deep_dir = File.join(%w(a b c d e))
    file = "sample.txt"
    deep_dir_path = File.join(@wc_path, deep_dir)
    path = File.join(deep_dir_path, file)
    tmp_deep_dir_path = File.join(@tmp_path, deep_dir)
    tmp_path = File.join(tmp_deep_dir_path, file)

    ctx = make_context(log)

    FileUtils.mkdir_p(tmp_deep_dir_path)
    File.open(tmp_path, "w") {|f| f.print(src)}

    ctx.import(@tmp_path, @repos_uri)

    ctx.up(@wc_path)
    assert_equal(src, File.open(path){|f| f.read})
  end
  
  def test_commit
    log = "sample log"
    dir1 = "dir1"
    dir2 = "dir2"
    dir1_path = File.join(@wc_path, dir1)
    dir2_path = File.join(dir1_path, dir2)
    
    ctx = make_context(log)
    assert_nil(ctx.commit(@wc_path))
    ctx.mkdir(dir1_path)
    assert_equal(0, youngest_rev)
    assert_equal(1, ctx.commit(@wc_path).revision)
    ctx.mkdir(dir2_path)
    assert_nil(ctx.commit(@wc_path, false))
    assert_equal(2, ctx.ci(@wc_path).revision)
  end

  def test_status
    log = "sample log"
    dir = "dir"
    dir_path = File.join(@wc_path, dir)
    
    ctx = make_context(log)

    ctx.mkdir(dir_path)
    infos = []
    rev = ctx.status(@wc_path) do |path, status|
      infos << [path, status]
    end

    assert_equal(0, rev)
    assert_equal(1, infos.size)
    result_path, status = infos.first
    assert_equal(result_path, dir_path)
    assert(status.text_added?)
    assert(status.entry.dir?)
    assert(status.entry.add?)
  end

  def test_checkout
    log = "sample log"
    file = "hello.txt"
    dir = "dir"
    dir_path = File.join(@wc_path, dir)
    path = File.join(dir_path, file)
    content = "Hello"

    ctx = make_context(log)
    ctx.mkdir(dir_path)
    File.open(path, "w"){|f| f.print(content)}
    ctx.add(path)
    ctx.commit(@wc_path)

    FileUtils.rm_rf(@wc_path)
    ctx.checkout(@repos_uri, @wc_path)
    assert(File.exist?(path))

    FileUtils.rm_rf(@wc_path)
    ctx.co(@repos_uri, @wc_path, nil, nil, false)
    assert(!File.exist?(path))
  end
  
  def test_update
    log = "sample log"
    file = "hello.txt"
    path = File.join(@wc_path, file)
    content = "Hello"
    File.open(path, "w"){|f| f.print(content)}

    ctx = make_context(log)
    
    assert_nothing_raised do
      ctx.update(File.join(@wc_path, "non-exist"), youngest_rev)
    end
    
    ctx.add(path)
    commit_info = ctx.commit(@wc_path)

    FileUtils.rm(path)
    assert(!File.exist?(path))
    assert_equal(commit_info.revision,
                 ctx.update(path, commit_info.revision))
    assert_equal(content, File.read(path))
    
    FileUtils.rm(path)
    assert(!File.exist?(path))
    assert_equal([commit_info.revision],
                 ctx.update([path], commit_info.revision))
    assert_equal(content, File.read(path))

    assert_raise(Svn::Error::FS_NO_SUCH_REVISION) do
      begin
        ctx.update(path, commit_info.revision + 1)
      ensure
        ctx.cleanup(@wc_path)
      end
    end
    assert_nothing_raised do
      ctx.update(path + "non-exist", commit_info.revision)
    end
  end

  def test_revert
    log = "sample log"
    file = "hello.txt"
    path = File.join(@wc_path, file)
    content = "Hello"
    File.open(path, "w"){|f| f.print(content)}

    ctx = make_context(log)
    ctx.add(path)
    commit_info = ctx.commit(@wc_path)

    assert_equal(content, File.read(path))
    File.open(path, "w"){}
    assert_equal("", File.read(path))
    ctx.revert(path)
    assert_equal(content, File.read(path))
  end

  def test_log
    log = "sample log"
    file = "hello.txt"
    path = File.join(@wc_path, file)
    FileUtils.touch(path)

    ctx = make_context(log)
    ctx.add(path)
    commit_info = ctx.commit(@wc_path)
    rev = commit_info.revision

    assert_equal(log, ctx.log_message(path, rev))
  end

  def test_blame
    log = "sample log"
    file = "hello.txt"
    srcs = %w(first second third)
    infos = []
    path = File.join(@wc_path, file)

    ctx = make_context(log)
    
    File.open(path, "w") {|f| f.puts(srcs[0])}
    ctx.add(path)
    commit_info = ctx.commit(@wc_path)
    infos << [0, commit_info.revision, @author, commit_info.date, srcs[0]]
    
    File.open(path, "a") {|f| f.puts(srcs[1])}
    commit_info = ctx.commit(@wc_path)
    infos << [1, commit_info.revision, @author, commit_info.date, srcs[1]]

    File.open(path, "a") {|f| f.puts(srcs[2])}
    commit_info = ctx.commit(@wc_path)
    infos << [2, commit_info.revision, @author, commit_info.date, srcs[2]]

    result = []
    ctx.blame(path) do |line_no, revision, author, date, line|
      result << [line_no, revision, author, date, line]
    end
    assert_equal(infos, result)


    ctx.prop_set(Svn::Core::PROP_MIME_TYPE, "image/DUMMY", path)
    ctx.commit(@wc_path)
    
    assert_raise(Svn::Error::CLIENT_IS_BINARY_FILE) do
      ctx.ann(path) {}
    end
  end

  def test_diff
    log = "sample log"
    before = "before\n"
    after = "after\n"
    file = "hello.txt"
    path = File.join(@wc_path, file)

    File.open(path, "w") {|f| f.print(before)}

    ctx = make_context(log)
    ctx.add(path)
    commit_info = ctx.commit(@wc_path)
    rev1 = commit_info.revision

    File.open(path, "w") {|f| f.print(after)}

    out_file = Tempfile.new("svn")
    err_file = Tempfile.new("svn")
    ctx.diff([], path, rev1, path, "WORKING", out_file.path, err_file.path)
    out_file.open
    assert_match(/-#{before}\+#{after}\z/, out_file.read)

    commit_info = ctx.commit(@wc_path)
    rev2 = commit_info.revision
    out_file = Tempfile.new("svn")
    ctx.diff([], path, rev1, path, rev2, out_file.path, err_file.path)
    out_file.open
    assert_match(/-#{before}\+#{after}\z/, out_file.read)
  end

  def test_diff_peg
    log = "sample log"
    before = "before\n"
    after = "after\n"
    file = "hello.txt"
    path = File.join(@wc_path, file)

    File.open(path, "w") {|f| f.print(before)}

    ctx = make_context(log)
    ctx.add(path)
    commit_info = ctx.commit(@wc_path)
    rev1 = commit_info.revision

    File.open(path, "w") {|f| f.print(after)}

    out_file = Tempfile.new("svn")
    err_file = Tempfile.new("svn")
    ctx.diff_peg([], path, rev1, "WORKING", out_file.path, err_file.path)
    out_file.open
    assert_match(/-#{before}\+#{after}\z/, out_file.read)

    commit_info = ctx.commit(@wc_path)
    rev2 = commit_info.revision
    out_file = Tempfile.new("svn")
    ctx.diff_peg([], path, rev1, rev2, out_file.path, err_file.path)
    out_file.open
    assert_match(/-#{before}\+#{after}\z/, out_file.read)
  end

  def test_merge
    log = "sample log"
    file = "sample.txt"
    src = "sample\n"
    trunk = File.join(@wc_path, "trunk")
    branch = File.join(@wc_path, "branch")
    trunk_path = File.join(trunk, file)
    branch_path = File.join(branch, file)

    ctx = make_context(log)
    ctx.mkdir(trunk, branch)
    File.open(trunk_path, "w") {}
    File.open(branch_path, "w") {}
    ctx.add(trunk_path)
    ctx.add(branch_path)
    rev1 = ctx.commit(@wc_path).revision

    File.open(branch_path, "w") {|f| f.print(src)}
    rev2 = ctx.commit(@wc_path).revision

    ctx.merge(branch, rev1, branch, rev2, trunk)
    rev3 = ctx.commit(@wc_path).revision

    assert_equal(src, ctx.cat(trunk_path, rev3))
    
    ctx.rm(branch_path)
    rev4 = ctx.commit(@wc_path).revision

    ctx.merge(branch, rev3, branch, rev4, trunk)
    assert(!File.exist?(trunk_path))

    ctx.revert(trunk_path)
    File.open(trunk_path, "a") {|f| f.print(src)}
    ctx.merge(branch, rev3, branch, rev4, trunk)
    assert(File.exist?(trunk_path))
    rev5 = ctx.commit(@wc_path).revision
    
    File.open(trunk_path, "a") {|f| f.print(src)}
    ctx.merge(branch, rev3, branch, rev4, trunk, true, false, true, true)
    assert(File.exist?(trunk_path))
    
    ctx.merge(branch, rev3, branch, rev4, trunk, true, false, true)
    rev6 = ctx.commit(@wc_path).revision

    assert(!File.exist?(trunk_path))
  end

  def test_merge_peg
    log = "sample log"
    file = "sample.txt"
    src = "sample\n"
    trunk = File.join(@wc_path, "trunk")
    branch = File.join(@wc_path, "branch")
    trunk_path = File.join(trunk, file)
    branch_path = File.join(branch, file)

    ctx = make_context(log)
    ctx.mkdir(trunk, branch)
    File.open(trunk_path, "w") {}
    File.open(branch_path, "w") {}
    ctx.add(trunk_path)
    ctx.add(branch_path)
    rev1 = ctx.commit(@wc_path).revision

    File.open(branch_path, "w") {|f| f.print(src)}
    rev2 = ctx.commit(@wc_path).revision

    ctx.merge_peg(branch, rev1, rev2, trunk)
    rev3 = ctx.commit(@wc_path).revision

    assert_equal(src, ctx.cat(trunk_path, rev3))
    
    ctx.rm(branch_path)
    rev4 = ctx.commit(@wc_path).revision

    ctx.merge_peg(branch, rev3, rev4, trunk)
    assert(!File.exist?(trunk_path))

    ctx.revert(trunk_path)
    File.open(trunk_path, "a") {|f| f.print(src)}
    ctx.merge_peg(branch, rev3, rev4, trunk)
    assert(File.exist?(trunk_path))
    rev5 = ctx.commit(@wc_path).revision
    
    File.open(trunk_path, "a") {|f| f.print(src)}
    ctx.merge_peg(branch, rev3, rev4, trunk, nil, true, false, true, true)
    assert(File.exist?(trunk_path))
    
    ctx.merge_peg(branch, rev3, rev4, trunk, nil, true, false, true)
    rev6 = ctx.commit(@wc_path).revision

    assert(!File.exist?(trunk_path))
  end

  def test_cleanup
    log = "sample log"
    file = "sample.txt"
    src = "sample\n"
    path = File.join(@wc_path, file)

    ctx = make_context(log)
    File.open(path, "w") {|f| f.print(src)}
    ctx.add(path)
    rev = ctx.commit(@wc_path).revision

    ctx.up(@wc_path, rev - 1)
    File.open(path, "w") {|f| f.print(src)}
    assert_raise(Svn::Error::WC_OBSTRUCTED_UPDATE) do
      ctx.up(@wc_path, rev)
    end

    assert_raise(Svn::Error::WC_LOCKED) do
      ctx.commit(@wc_path)
    end

    ctx.set_cancel_func do
      raise Svn::Error::CANCELLED
    end
    assert_raise(Svn::Error::CANCELLED) do
      ctx.cleanup(@wc_path)
    end
    assert_raise(Svn::Error::WC_LOCKED) do
      ctx.commit(@wc_path)
    end

    ctx.set_cancel_func(nil)
    assert_nothing_raised do
      ctx.cleanup(@wc_path)
    end
    assert_nothing_raised do
      ctx.commit(@wc_path)
    end
  end
  
  def test_cat
    log = "sample log"
    src1 = "source1\n"
    src2 = "source2\n"
    file = "sample.txt"
    path = File.join(@wc_path, file)

    File.open(path, "w") {|f| f.print(src1)}

    ctx = make_context(log)
    ctx.add(path)
    commit_info = ctx.commit(@wc_path)
    rev1 = commit_info.revision

    assert_equal(src1, ctx.cat(path, rev1))
    assert_equal(src1, ctx.cat(path))
    
    File.open(path, "w") {|f| f.print(src2)}

    commit_info = ctx.commit(@wc_path)
    rev2 = commit_info.revision

    assert_equal(src1, ctx.cat(path, rev1))
    assert_equal(src2, ctx.cat(path, rev2))
    assert_equal(src2, ctx.cat(path))
  end

  def test_revprop
    log = "sample log"
    new_log = "new sample log"
    src = "source\n"
    file = "sample.txt"
    path = File.join(@wc_path, file)

    File.open(path, "w") {|f| f.print(src)}

    ctx = make_context(log)
    ctx.add(path)
    info = ctx.commit(@wc_path)

    assert_equal([log, info.revision],
                 ctx.revprop_get(Svn::Core::PROP_REVISION_LOG,
                                 @repos_uri, info.revision))
    assert_equal(log,
                 ctx.revprop(Svn::Core::PROP_REVISION_LOG,
                             @repos_uri, info.revision))

    assert_equal(info.revision,
                 ctx.revprop_set(Svn::Core::PROP_REVISION_LOG, new_log,
                                 @repos_uri, info.revision))
    assert_equal([new_log, info.revision],
                 ctx.revprop_get(Svn::Core::PROP_REVISION_LOG,
                                 @repos_uri, info.revision))
    assert_equal(new_log,
                 ctx.revprop(Svn::Core::PROP_REVISION_LOG,
                             @repos_uri, info.revision))

    assert_equal(info.revision,
                 ctx.revprop_del(Svn::Core::PROP_REVISION_LOG,
                                 @repos_uri, info.revision))
    assert_equal([nil, info.revision],
                 ctx.revprop_get(Svn::Core::PROP_REVISION_LOG,
                                 @repos_uri, info.revision))
    assert_equal(nil,
                 ctx.revprop(Svn::Core::PROP_REVISION_LOG,
                             @repos_uri, info.revision))
  end
  
  def test_switch
    log = "sample log"
    trunk_src = "trunk source\n"
    tag_src = "tag source\n"
    file = "sample.txt"
    file = "sample.txt"
    trunk_dir = "trunk"
    tag_dir = "tags"
    tag_name = "0.0.1"
    trunk_repos_uri = "#{@repos_uri}/#{trunk_dir}"
    tag_repos_uri = "#{@repos_uri}/#{tag_dir}/#{tag_name}"
    trunk_dir_path = File.join(@wc_path, trunk_dir)
    tag_dir_path = File.join(@wc_path, tag_dir)
    tag_name_dir_path = File.join(@wc_path, tag_dir, tag_name)
    trunk_path = File.join(trunk_dir_path, file)
    tag_path = File.join(tag_name_dir_path, file)
    path = File.join(@wc_path, file)

    ctx = make_context(log)

    ctx.mkdir(trunk_dir_path)
    File.open(trunk_path, "w") {|f| f.print(trunk_src)}
    ctx.add(trunk_path)
    trunk_rev = ctx.commit(@wc_path).revision
    
    ctx.mkdir(tag_dir_path, tag_name_dir_path)
    File.open(tag_path, "w") {|f| f.print(tag_src)}
    ctx.add(tag_path)
    tag_rev = ctx.commit(@wc_path).revision

    assert_equal(youngest_rev, ctx.switch(@wc_path, trunk_repos_uri))
    assert_equal(trunk_src, ctx.cat(path))

    assert_equal(youngest_rev, ctx.switch(@wc_path, tag_repos_uri))
    assert_equal(tag_src, ctx.cat(path))


    notify_info = []
    ctx.set_notify_func2 do |notify|
      notify_info << [notify.path, notify.action]
    end
    
    assert_equal(trunk_rev, ctx.switch(@wc_path, trunk_repos_uri, trunk_rev))
    assert_equal(trunk_src, ctx.cat(path))
    assert_equal([
                   [path, Svn::Wc::NOTIFY_UPDATE_UPDATE],
                   [@wc_path, Svn::Wc::NOTIFY_UPDATE_UPDATE],
                   [@wc_path, Svn::Wc::NOTIFY_UPDATE_COMPLETED],
                 ],
                 notify_info)

    notify_info.clear
    assert_equal(tag_rev, ctx.switch(@wc_path, tag_repos_uri, tag_rev))
    assert_equal(tag_src, ctx.cat(path))
    assert_equal([
                   [path, Svn::Wc::NOTIFY_UPDATE_UPDATE],
                   [@wc_path, Svn::Wc::NOTIFY_UPDATE_UPDATE],
                   [@wc_path, Svn::Wc::NOTIFY_UPDATE_COMPLETED],
                 ],
                 notify_info)
  end

  def test_authentication
    log = "sample log"
    src = "source\n"
    file = "sample.txt"
    path = File.join(@wc_path, file)
    svnserve_uri = "#{@repos_svnserve_uri}/#{file}"

    File.open(path, "w") {|f| f.print(src)}

    ctx = make_context(log)
    ctx.add(path)
    ctx.commit(@wc_path)

    ctx = Svn::Client::Context.new
    
    assert_raises(Svn::Error::AUTHN_NO_PROVIDER) do
      ctx.cat(svnserve_uri)
    end
    
    ctx.add_simple_prompt_provider(0) do |cred, realm, username, may_save|
      cred.username = "wrong-#{@author}"
      cred.password = @password
      cred.may_save = false
    end
    assert_raises(Svn::Error::RA_NOT_AUTHORIZED) do
      ctx.cat(svnserve_uri)
    end
    
    ctx.add_simple_prompt_provider(0) do |cred, realm, username, may_save|
      cred.username = @author
      cred.password = "wrong-#{@password}"
      cred.may_save = false
    end
    assert_raises(Svn::Error::RA_NOT_AUTHORIZED) do
      ctx.cat(svnserve_uri)
    end
    
    ctx.add_simple_prompt_provider(0) do |cred, realm, username, may_save|
      cred.username = @author
      cred.password = @password
      cred.may_save = false
    end
    assert_equal(src, ctx.cat(svnserve_uri))
  end

  def test_simple_provider
    log = "sample log"
    src = "source\n"
    file = "sample.txt"
    path = File.join(@wc_path, file)
    svnserve_uri = "#{@repos_svnserve_uri}/#{file}"
    
    File.open(path, "w") {|f| f.print(src)}

    ctx = make_context(log)
    setup_auth_baton(ctx.auth_baton)
    ctx.add(path)
    ctx.commit(@wc_path)

    ctx = Svn::Client::Context.new
    setup_auth_baton(ctx.auth_baton)
    ctx.add_simple_provider
    assert_raises(Svn::Error::RA_NOT_AUTHORIZED) do
      assert_equal(src, ctx.cat(svnserve_uri))
    end

    ctx = Svn::Client::Context.new
    setup_auth_baton(ctx.auth_baton)
    ctx.add_simple_provider
    ctx.add_simple_prompt_provider(0) do |cred, realm, username, may_save|
      cred.username = @author
      cred.password = @password
      cred.may_save = true
    end
    assert_equal(src, ctx.cat(svnserve_uri))

    ctx = Svn::Client::Context.new
    setup_auth_baton(ctx.auth_baton)
    ctx.add_simple_provider
    assert_equal(src, ctx.cat(svnserve_uri))
  end

  def test_windows_simple_provider
    return unless Svn::Client.respond_to?(:add_windows_simple_provider)

    log = "sample log"
    src = "source\n"
    file = "sample.txt"
    path = File.join(@wc_path, file)
    svnserve_uri = "#{@repos_svnserve_uri}/#{file}"
    
    File.open(path, "w") {|f| f.print(src)}

    ctx = make_context(log)
    setup_auth_baton(ctx.auth_baton)
    ctx.add(path)
    ctx.commit(@wc_path)

    ctx = Svn::Client::Context.new
    setup_auth_baton(ctx.auth_baton)
    ctx.add_windows_simple_provider
    assert_raises(Svn::Error::RA_NOT_AUTHORIZED) do
      assert_equal(src, ctx.cat(svnserve_uri))
    end

    ctx = Svn::Client::Context.new
    setup_auth_baton(ctx.auth_baton)
    ctx.add_simple_provider
    ctx.add_simple_prompt_provider(0) do |cred, realm, username, may_save|
      cred.username = @author
      cred.password = @password
      cred.may_save = true
    end
    assert_equal(src, ctx.cat(svnserve_uri))

    ctx = Svn::Client::Context.new
    setup_auth_baton(ctx.auth_baton)
    ctx.add_windows_simple_provider
    assert_equal(src, ctx.cat(svnserve_uri))
  end
  
  def test_username_provider
    log = "sample log"
    new_log = "sample new log"
    src = "source\n"
    file = "sample.txt"
    path = File.join(@wc_path, file)
    repos_uri = "#{@repos_uri}/#{file}"

    File.open(path, "w") {|f| f.print(src)}

    ctx = make_context(log)
    ctx.add(path)
    info = ctx.commit(@wc_path)

    ctx = Svn::Client::Context.new
    setup_auth_baton(ctx.auth_baton)
    ctx.auth_baton[Svn::Core::AUTH_PARAM_DEFAULT_USERNAME] = @author
    ctx.add_username_provider
    assert_nothing_raised do
      ctx.revprop_set(Svn::Core::PROP_REVISION_LOG, new_log,
                      repos_uri, info.revision)
    end

    ctx = Svn::Client::Context.new
    setup_auth_baton(ctx.auth_baton)
    ctx.auth_baton[Svn::Core::AUTH_PARAM_DEFAULT_USERNAME] = "#{@author}-NG"
    ctx.add_username_provider
    assert_raise(Svn::Error::REPOS_HOOK_FAILURE) do
      ctx.revprop_set(Svn::Core::PROP_REVISION_LOG, new_log,
                      repos_uri, info.revision)
    end

    ctx = Svn::Client::Context.new
    setup_auth_baton(ctx.auth_baton)
    ctx.auth_baton[Svn::Core::AUTH_PARAM_DEFAULT_USERNAME] = nil
    ctx.add_username_prompt_provider(0) do |cred, realm, may_save|
    end
    assert_raise(Svn::Error::REPOS_HOOK_FAILURE) do
      ctx.revprop_set(Svn::Core::PROP_REVISION_LOG, new_log,
                      repos_uri, info.revision)
    end

    ctx = Svn::Client::Context.new
    setup_auth_baton(ctx.auth_baton)
    ctx.auth_baton[Svn::Core::AUTH_PARAM_DEFAULT_USERNAME] = nil
    ctx.add_username_prompt_provider(0) do |cred, realm, may_save|
      cred.username = @author
    end
    assert_nothing_raised do
      ctx.revprop_set(Svn::Core::PROP_REVISION_LOG, new_log,
                      repos_uri, info.revision)
    end
  end
  
  def test_not_new
    assert_raise(NoMethodError) do
      Svn::Client::CommitItem.new
    end
    assert_raise(NoMethodError) do
      Svn::Client::CommitInfo.new
    end
  end
end
