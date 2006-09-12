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

    infos = []
    ctx.set_notify_func do |notify|
      infos << [notify.path, notify]
    end
    
    dirs_path.each do |path|
      assert(!File.exist?(path))
    end
    ctx.mkdir(dirs_path)
    assert_equal(dirs_path.sort,
                 infos.collect{|path, notify| path}.sort)
    assert_equal(dirs_path.collect{true},
                 infos.collect{|path, notify| notify.add?})
    dirs_path.each do |path|
      assert(File.exist?(path))
    end

    infos.clear
    ctx.commit(@wc_path)
    assert_equal(dirs_path.sort,
                 infos.collect{|path, notify| path}.sort)
    assert_equal(dirs_path.collect{true},
                 infos.collect{|path, notify| notify.commit_added?})
  end

  def test_mkdir_multiple2
    log = "sample log"
    dir = "dir"
    dir2 = "dir2"
    dirs = [dir, dir2]
    dirs_path = dirs.collect{|d| File.join(@wc_path, d)}
    dirs_uri = dirs.collect{|d| "#{@repos_uri}/#{d}"}

    ctx = make_context(log)

    infos = []
    ctx.set_notify_func do |notify|
      infos << [notify.path, notify]
    end
    
    dirs_path.each do |path|
      assert(!File.exist?(path))
    end
    ctx.mkdir(*dirs_path)
    assert_equal(dirs_path.sort,
                 infos.collect{|path, notify| path}.sort)
    assert_equal(dirs_path.collect{true},
                 infos.collect{|path, notify| notify.add?})
    dirs_path.each do |path|
      assert(File.exist?(path))
    end

    infos.clear
    ctx.commit(@wc_path)
    assert_equal(dirs_path.sort,
                 infos.collect{|path, notify| path}.sort)
    assert_equal(dirs_path.collect{true},
                 infos.collect{|path, notify| notify.commit_added?})
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
    gc_disable do
      assert_raises(Svn::Error::CLIENT_MODIFIED) do
        ctx.delete(path)
      end
      assert_raises(Svn::Error::WC_LOCKED) do
        ctx.delete(path, true)
      end
      ctx.cleanup(@wc_path)
      ctx.delete(path, true)
      ctx.commit(@wc_path)
    end
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
    gc_disable do
      assert_raises(Svn::Error::CLIENT_MODIFIED) do
        ctx.rm(path)
      end
      assert_raises(Svn::Error::WC_LOCKED) do
        ctx.rm_f(path)
      end
      ctx.cleanup(@wc_path)
      ctx.rm_f(path)
      ctx.commit(@wc_path)
    end
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
    file1 = "sample1.txt"
    file2 = "sample2.txt"
    dir = "dir"
    dir_path = File.join(@wc_path, dir)
    path1 = File.join(@wc_path, file1)
    path2 = File.join(dir_path, file2)
    
    ctx = make_context(log)
    File.open(path1, "w") {}
    ctx.add(path1)
    rev1 = ctx.commit(@wc_path).revision

    
    ctx.mkdir(dir_path)
    File.open(path2, "w") {}
    
    infos = []
    rev = ctx.status(@wc_path) do |path, status|
      infos << [path, status]
    end

    assert_equal(youngest_rev, rev)
    assert_equal([dir_path, path2].sort,
                 infos.collect{|path, status| path}.sort)
    dir_status = infos.assoc(dir_path).last
    assert(dir_status.text_added?)
    assert(dir_status.entry.dir?)
    assert(dir_status.entry.add?)
    path2_status = infos.assoc(path2).last
    assert(!path2_status.text_added?)
    assert_nil(path2_status.entry)


    infos = []
    rev = ctx.st(@wc_path, rev1, true, true) do |path, status|
      infos << [path, status]
    end
    
    assert_equal(rev1, rev)
    assert_equal([@wc_path, dir_path, path1, path2].sort,
                 infos.collect{|path, status| path}.sort)
    wc_status = infos.assoc(@wc_path).last
    assert(wc_status.text_normal?)
    assert(wc_status.entry.dir?)
    assert(wc_status.entry.normal?)
    dir_status = infos.assoc(dir_path).last
    assert(dir_status.text_added?)
    assert(dir_status.entry.dir?)
    assert(dir_status.entry.add?)
    path1_status = infos.assoc(path1).last
    assert(path1_status.text_normal?)
    assert(path1_status.entry.file?)
    assert(path1_status.entry.normal?)
    path2_status = infos.assoc(path2).last
    assert(!path2_status.text_added?)
    assert_nil(path2_status.entry)


    ctx.prop_set(Svn::Core::PROP_IGNORE, file2, dir_path)

    infos = []
    rev = ctx.status(@wc_path, nil, true, true, true, false) do |path, status|
      infos << [path, status]
    end
    
    assert_equal(rev1, rev)
    assert_equal([@wc_path, dir_path, path1].sort,
                 infos.collect{|path, status| path}.sort)


    infos = []
    rev = ctx.status(@wc_path, nil, true, true, true, true) do |path, status|
      infos << [path, status]
    end
    
    assert_equal(rev1, rev)
    assert_equal([@wc_path, dir_path, path1, path2].sort,
                 infos.collect{|path, status| path}.sort)
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
    file1 = "hello1.txt"
    file2 = "hello2.txt"
    file3 = "hello3.txt"
    dir = "dir"
    dir_path = File.join(@wc_path, dir)
    path1 = File.join(@wc_path, file1)
    path2 = File.join(@wc_path, file2)
    path3 = File.join(dir_path, file3)
    content = "Hello"

    ctx = make_context(log)
    File.open(path1, "w"){|f| f.print(content)}
    File.open(path2, "w"){|f| f.print(content)}
    ctx.add(path1)
    ctx.add(path2)
    ctx.mkdir(dir_path)
    File.open(path3, "w"){|f| f.print(content)}
    ctx.add(path3)
    commit_info = ctx.commit(@wc_path)

    File.open(path1, "w"){}
    assert_equal("", File.open(path1){|f| f.read})

    ctx.revert(path1)
    assert_equal(content, File.open(path1){|f| f.read})

    File.open(path1, "w"){}
    File.open(path2, "w"){}
    assert_equal("", File.open(path1){|f| f.read})
    assert_equal("", File.open(path2){|f| f.read})
    ctx.revert([path1, path2])
    assert_equal(content, File.open(path1){|f| f.read})
    assert_equal(content, File.open(path2){|f| f.read})

    File.open(path1, "w"){}
    File.open(path2, "w"){}
    File.open(path3, "w"){}
    assert_equal("", File.open(path1){|f| f.read})
    assert_equal("", File.open(path2){|f| f.read})
    assert_equal("", File.open(path3){|f| f.read})
    ctx.revert(@wc_path)
    assert_equal(content, File.open(path1){|f| f.read})
    assert_equal(content, File.open(path2){|f| f.read})
    assert_equal(content, File.open(path3){|f| f.read})
    
    File.open(path1, "w"){}
    File.open(path2, "w"){}
    File.open(path3, "w"){}
    assert_equal("", File.open(path1){|f| f.read})
    assert_equal("", File.open(path2){|f| f.read})
    assert_equal("", File.open(path3){|f| f.read})
    ctx.revert(@wc_path, false)
    assert_equal("", File.open(path1){|f| f.read})
    assert_equal("", File.open(path2){|f| f.read})
    assert_equal("", File.open(path3){|f| f.read})

    File.open(path1, "w"){}
    File.open(path2, "w"){}
    File.open(path3, "w"){}
    assert_equal("", File.open(path1){|f| f.read})
    assert_equal("", File.open(path2){|f| f.read})
    assert_equal("", File.open(path3){|f| f.read})
    ctx.revert(dir_path)
    assert_equal("", File.open(path1){|f| f.read})
    assert_equal("", File.open(path2){|f| f.read})
    assert_equal(content, File.open(path3){|f| f.read})
  end

  def test_log
    log1 = "sample log1"
    log2 = "sample log2"
    log3 = "sample log3"
    src1 = "source1\n"
    src2 = "source2\n"
    src3 = "source3\n"
    file1 = "sample1.txt"
    file2 = "sample2.txt"
    file3 = "sample3.txt"
    path1 = File.join(@wc_path, file1)
    path2 = File.join(@wc_path, file2)
    path3 = File.join(@wc_path, file3)
    abs_path1 = File.join('', file1)
    abs_path2 = File.join('', file2)
    abs_path3 = File.join('', file3)

    ctx = make_context(log1)
    File.open(path1, "w") {|f| f.print(src1)}
    ctx.add(path1)
    rev1 = ctx.ci(@wc_path).revision

    ctx = make_context(log2)
    ctx.cp(path1, path2)
    rev2 = ctx.ci(@wc_path).revision

    ctx = make_context(log3)
    ctx.cp(path1, path3)
    File.open(path1, "w") {|f| f.print(src2)}
    File.open(path3, "w") {|f| f.print(src3)}
    rev3 = ctx.ci(@wc_path).revision

    changed_paths_lists = {}
    revs = {}
    messages = {}
    keys = [@wc_path, path1, path2, path3]
    keys.each do |key|
      revs[key] = []
      changed_paths_lists[key] = []
      messages[key] = []
      args = [key, 1, "HEAD", 0, true, nil]
      ctx.log(*args) do |changed_paths, rev, author, date, message|
        revs[key] << rev
        changed_paths_lists[key] << changed_paths
        messages[key] << message
      end
    end
    changed_paths_list = changed_paths_lists[@wc_path]

    assert_equal([rev1, rev2, rev3], revs[@wc_path])
    assert_equal([rev1, rev3], revs[path1])
    assert_equal([rev1, rev2], revs[path2])
    assert_equal([rev1, rev3], revs[path3])
    assert_equal([log1, log2, log3], messages[@wc_path])

    expected = [[abs_path1], [abs_path2], [abs_path1, abs_path3]]
    actual = changed_paths_list.collect {|changed_paths| changed_paths.keys}
    assert_nested_sorted_array(expected, actual)

    assert_equal('A', changed_paths_list[0][abs_path1].action)
    assert_false(changed_paths_list[0][abs_path1].copied?)
    assert_equal('A', changed_paths_list[1][abs_path2].action)
    assert_true(changed_paths_list[1][abs_path2].copied?)
    assert_equal(abs_path1, changed_paths_list[1][abs_path2].copyfrom_path)
    assert_equal(rev1, changed_paths_list[1][abs_path2].copyfrom_rev)
    assert_equal('M', changed_paths_list[2][abs_path1].action)
    assert_equal('A', changed_paths_list[2][abs_path3].action)
  end

  def test_log_message
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

  def test_diff_summarize
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

    commit_info = ctx.commit(@wc_path)
    rev2 = commit_info.revision

    diffs = []
    ctx.diff_summarize(path, rev1, path, rev2) do |diff|
      diffs << diff
    end
    assert_equal([file], diffs.collect {|d| d.path})
    kinds = diffs.collect do |d|
      [d.kind_normal?, d.kind_added?, d.kind_modified?, d.kind_deleted?]
    end
    assert_equal([[false, false, true, false]], kinds)
    assert_equal([false], diffs.collect {|d| d.prop_changed?})
    node_kinds = diffs.collect do |d|
      [d.node_kind_none?, d.node_kind_file?,
       d.node_kind_dir?, d.node_kind_unknown?]
    end
    assert_equal([[false, true, false, false]], node_kinds)
  end

  def test_diff_summarize_peg
    log = "sample log"
    before = "before\n"
    after = "after\n"
    before_file = "before.txt"
    after_file = "after.txt"
    moved_file = "moved.txt"
    before_path = File.join(@wc_path, before_file)
    after_path = File.join(@wc_path, after_file)
    moved_path = File.join(@wc_path, moved_file)
    after_uri = "#{@repos_uri}/#{after_file}"

    File.open(before_path, "w") {|f| f.print(before)}

    ctx = make_context(log)
    ctx.add(before_path)
    commit_info = ctx.commit(@wc_path)
    rev1 = commit_info.revision

    ctx.mv(before_path, after_path)
    commit_info = ctx.commit(@wc_path)
    rev2 = commit_info.revision

    File.open(after_path, "w") {|f| f.print(after)}
    commit_info = ctx.commit(@wc_path)
    rev3 = commit_info.revision

    File.open(after_path, "w") {|f| f.print(before)}
    commit_info = ctx.commit(@wc_path)
    rev4 = commit_info.revision

    ctx.mv(after_path, moved_path)
    commit_info = ctx.commit(@wc_path)
    rev5 = commit_info.revision

    diffs = []
    ctx.diff_summarize_peg(after_uri, rev3, rev4, rev3) do |diff|
      diffs << diff
    end
    assert_equal([after_file], diffs.collect {|d| d.path})
    kinds = diffs.collect do |d|
      [d.kind_normal?, d.kind_added?, d.kind_modified?, d.kind_deleted?]
    end
    assert_equal([[false, false, true, false]], kinds)
    assert_equal([false], diffs.collect {|d| d.prop_changed?})
    node_kinds = diffs.collect do |d|
      [d.node_kind_none?, d.node_kind_file?,
       d.node_kind_dir?, d.node_kind_unknown?]
    end
    assert_equal([[false, true, false, false]], node_kinds)
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
    
    gc_disable do
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
  end

  def test_relocate
    log = "sample log"
    file = "sample.txt"
    src = "sample\n"
    path = File.join(@wc_path, file)

    ctx = make_context(log)
    File.open(path, "w") {|f| f.print(src)}
    ctx.add(path)
    ctx.commit(@wc_path)

    assert_nothing_raised do
      ctx.cat(path)
    end

    ctx.add_simple_prompt_provider(0) do |cred, realm, username, may_save|
      cred.username = @author
      cred.password = @password
      cred.may_save = true
    end
    ctx.relocate(@wc_path, @repos_uri, @repos_svnserve_uri)
    
    ctx = make_context(log)
    assert_raises(Svn::Error::AUTHN_NO_PROVIDER) do
      ctx.cat(path)
    end
  end

  def test_resolved
    log = "sample log"
    file = "sample.txt"
    dir = "dir"
    src1 = "before\n"
    src2 = "after\n"
    dir_path = File.join(@wc_path, dir)
    path = File.join(dir_path, file)

    ctx = make_context(log)
    ctx.mkdir(dir_path)
    File.open(path, "w") {}
    ctx.add(path)
    rev1 = ctx.ci(@wc_path).revision

    File.open(path, "w") {|f| f.print(src1)}
    rev2 = ctx.ci(@wc_path).revision

    ctx.up(@wc_path, rev1)

    File.open(path, "w") {|f| f.print(src2)}
    ctx.up(@wc_path)

    assert_raises(Svn::Error::WC_FOUND_CONFLICT) do
      ctx.ci(@wc_path)
    end

    ctx.resolved(dir_path, false)
    assert_raises(Svn::Error::WC_FOUND_CONFLICT) do
      ctx.ci(@wc_path)
    end

    ctx.resolved(dir_path)
    info = nil
    assert_nothing_raised do
      info = ctx.ci(@wc_path)
    end
    assert_not_nil(info)
    assert_equal(rev2 + 1, info.revision)
  end

  def test_copy
    log = "sample log"
    src = "source\n"
    file1 = "sample1.txt"
    file2 = "sample2.txt"
    path1 = File.join(@wc_path, file1)
    path2 = File.join(@wc_path, file2)

    ctx = make_context(log)
    File.open(path1, "w") {|f| f.print(src)}
    ctx.add(path1)

    ctx.ci(@wc_path)

    ctx.cp(path1, path2)

    infos = []
    ctx.set_notify_func do |notify|
      infos << [notify.path, notify]
    end
    ctx.ci(@wc_path)

    assert_equal([path2].sort,
                 infos.collect{|path, notify| path}.sort)
    path2_notify = infos.assoc(path2)[1]
    assert(path2_notify.commit_added?)
    assert_equal(File.open(path1) {|f| f.read},
                 File.open(path2) {|f| f.read})
  end
  
  def test_move
    log = "sample log"
    src = "source\n"
    file1 = "sample1.txt"
    file2 = "sample2.txt"
    path1 = File.join(@wc_path, file1)
    path2 = File.join(@wc_path, file2)

    ctx = make_context(log)
    File.open(path1, "w") {|f| f.print(src)}
    ctx.add(path1)

    ctx.ci(@wc_path)

    ctx.mv(path1, path2)

    infos = []
    ctx.set_notify_func do |notify|
      infos << [notify.path, notify]
    end
    ctx.ci(@wc_path)

    assert_equal([path1, path2].sort,
                 infos.collect{|path, notify| path}.sort)
    path1_notify = infos.assoc(path1)[1]
    assert(path1_notify.commit_deleted?)
    path2_notify = infos.assoc(path2)[1]
    assert(path2_notify.commit_added?)
    assert_equal(src, File.open(path2) {|f| f.read})
  end
  
  def test_move_force
    log = "sample log"
    src1 = "source1\n"
    src2 = "source2\n"
    file1 = "sample1.txt"
    file2 = "sample2.txt"
    path1 = File.join(@wc_path, file1)
    path2 = File.join(@wc_path, file2)

    ctx = make_context(log)
    File.open(path1, "w") {|f| f.print(src1)}
    ctx.add(path1)

    ctx.ci(@wc_path)

    File.open(path1, "w") {|f| f.print(src2)}

    assert_raises(Svn::Error::CLIENT_MODIFIED) do
      ctx.mv(path1, path2)
    end
    ctx.cleanup(@wc_path)

    assert_nothing_raised do
      ctx.mv_f(path1, path2)
    end

    notifies = []
    ctx.set_notify_func do |notify|
      notifies << notify
    end
    ctx.ci(@wc_path)

    paths = notifies.collect do |notify|
      notify.path
    end
    assert_equal([path1, path2, path2].sort, paths.sort)

    deleted_paths = notifies.find_all do |notify|
      notify.commit_deleted?
    end.collect do |notify|
      notify.path
    end
    assert_equal([path1].sort, deleted_paths.sort)

    added_paths = notifies.find_all do |notify|
      notify.commit_added?
    end.collect do |notify|
      notify.path
    end
    assert_equal([path2].sort, added_paths.sort)

    postfix_txdelta_paths = notifies.find_all do |notify|
      notify.commit_postfix_txdelta?
    end.collect do |notify|
      notify.path
    end
    assert_equal([path2].sort, postfix_txdelta_paths.sort)

    assert_equal(src2, File.open(path2) {|f| f.read})
  end

  def test_prop
    log = "sample log"
    dir = "dir"
    file = "sample.txt"
    dir_path = File.join(@wc_path, dir)
    dir_uri = "#{@repos_uri}/#{dir}"
    path = File.join(dir_path, file)
    uri = "#{dir_uri}/#{file}"
    prop_name = "sample-prop"
    prop_value = "sample value"
    invalid_mime_type_prop_value = "image"

    ctx = make_context(log)

    ctx.mkdir(dir_path)
    File.open(path, "w") {}
    ctx.add(path)

    ctx.commit(@wc_path)

    assert_equal({}, ctx.prop_get(prop_name, path))
    ctx.prop_set(prop_name, prop_value, path)
    ctx.commit(@wc_path)
    assert_equal({uri => prop_value}, ctx.pget(prop_name, path))
    
    ctx.prop_del(prop_name, path)
    ctx.commit(@wc_path)
    assert_equal({}, ctx.pg(prop_name, path))
    
    ctx.ps(prop_name, prop_value, path)
    ctx.commit(@wc_path)
    assert_equal({uri => prop_value}, ctx.pg(prop_name, path))

    ctx.ps(prop_name, nil, path)
    ctx.commit(@wc_path)
    assert_equal({}, ctx.pg(prop_name, path))

    ctx.up(@wc_path)
    ctx.ps(prop_name, prop_value, dir_path)
    ctx.ci(@wc_path)
    assert_equal({
                   dir_uri => prop_value,
                   uri => prop_value,
                 },
                 ctx.pg(prop_name, dir_path))

    ctx.up(@wc_path)
    ctx.pdel(prop_name, dir_path, false)
    ctx.ci(@wc_path)
    assert_equal({uri => prop_value}, ctx.pg(prop_name, dir_path))
    
    ctx.up(@wc_path)
    ctx.pd(prop_name, dir_path)
    ctx.ci(@wc_path)
    assert_equal({}, ctx.pg(prop_name, dir_path))
    
    ctx.up(@wc_path)
    ctx.ps(prop_name, prop_value, dir_path, false)
    ctx.ci(@wc_path)
    assert_equal({dir_uri => prop_value}, ctx.pg(prop_name, dir_path))

    assert_raises(Svn::Error::BAD_MIME_TYPE) do
      ctx.ps(Svn::Core::PROP_MIME_TYPE,
             invalid_mime_type_prop_value,
             path)
    end
    ctx.cleanup(@wc_path)

    assert_nothing_raised do
      ctx.ps(Svn::Core::PROP_MIME_TYPE,
             invalid_mime_type_prop_value,
             path, false, true)
    end
    ctx.commit(@wc_path)
    assert_equal({uri => invalid_mime_type_prop_value},
                 ctx.pg(Svn::Core::PROP_MIME_TYPE, path))
  end
  
  def test_prop_list
    log = "sample log"
    dir = "dir"
    file = "sample.txt"
    dir_path = File.join(@wc_path, dir)
    path = File.join(dir_path, file)
    dir_uri = "#{@repos_uri}/#{dir}"
    uri = "#{dir_uri}/#{file}"
    name1 = "name1"
    name2 = "name2"
    value1 = "value1"
    value2 = "value2"

    ctx = make_context(log)

    ctx.mkdir(dir_path)
    File.open(path, "w") {}
    ctx.add(path)

    ctx.ci(@wc_path)

    assert_equal([], ctx.prop_list(path))
    
    ctx.ps(name1, value1, path)
    ctx.ci(@wc_path)
    assert_equal([uri], ctx.prop_list(path).collect{|item| item.node_name})
    assert_equal([{name1 => value1}],
                 ctx.plist(path).collect{|item| item.prop_hash})
    assert_equal([value1], ctx.pl(path).collect{|item| item[name1]})

    ctx.up(@wc_path)
    ctx.ps(name2, value2, dir_path)
    ctx.ci(@wc_path)
    assert_equal([uri, dir_uri].sort,
                 ctx.prop_list(dir_path).collect{|item| item.name})
    prop_list = ctx.plist(dir_path).collect{|item| [item.name, item.props]}
    props = prop_list.assoc(uri)[1]
    dir_props = prop_list.assoc(dir_uri)[1]
    assert_equal({name1 => value1, name2 => value2}, props)
    assert_equal({name2 => value2}, dir_props)
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

  def test_lock
    log = "sample log"
    src = "source\n"
    file = "sample.txt"
    path = File.join(@wc_path, file)

    File.open(path, "w") {|f| f.print(src)}

    ctx = make_context(log)
    ctx.add(path)
    ctx.commit(@wc_path)

    infos = []
    ctx.set_notify_func do |notify|
      infos << [notify.path, notify]
    end
    ctx.lock(path)

    assert_equal([file], infos.collect{|path, notify| path})
    file_notify = infos.assoc(file)[1]
    assert(file_notify.locked?)
  end
  
  def test_unlock
    log = "sample log"
    src = "source\n"
    file = "sample.txt"
    path = File.join(@wc_path, file)

    File.open(path, "w") {|f| f.print(src)}

    ctx = make_context(log)
    ctx.add(path)
    ctx.commit(@wc_path)

    ctx.lock(path)

    infos = []
    ctx.set_notify_func do |notify|
      infos << [notify.path, notify]
    end
    ctx.unlock(path)
    assert_equal([file], infos.collect{|path, notify| path})
    file_notify = infos.assoc(file)[1]
    assert(file_notify.unlocked?)
  end

  def test_info
    log = "sample log"
    ctx = make_context(log)
    repos_base = File.basename(@repos_path)

    infos = []
    ctx.info(@wc_path) do |path, info|
      infos << [path, info]
    end
    assert_equal([repos_base],
                 infos.collect{|path, info| path})
    top_info = infos.assoc(repos_base)[1]
    assert_equal(@repos_uri, top_info.url)
  end

  def test_url_from_path
    log = "sample log"
    ctx = make_context(log)
    assert_equal(@repos_uri, ctx.url_from_path(@wc_path))
    assert_equal(@repos_uri, Svn::Client.url_from_path(@wc_path))
  end
  
  def test_uuid
    log = "sample log"
    ctx = make_context(log)
    Svn::Wc::AdmAccess.open(nil, @wc_path, false, 0) do |adm|
      assert_equal(ctx.uuid_from_url(@repos_uri),
                   ctx.uuid_from_path(@wc_path, adm))
    end
  end

  def test_open_ra_session
    log = "sample log"
    ctx = make_context(log)

    assert_instance_of(Svn::Ra::Session, ctx.open_ra_session(@repos_uri))
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

    assert_equal([
                   {
                     Svn::Core::PROP_REVISION_AUTHOR => @author,
                     Svn::Core::PROP_REVISION_DATE => info.date,
                     Svn::Core::PROP_REVISION_LOG => log,
                   },
                   info.revision
                 ],
                 ctx.revprop_list(@repos_uri, info.revision))
    
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
                 ctx.rpget(Svn::Core::PROP_REVISION_LOG,
                           @repos_uri, info.revision))
    assert_equal(new_log,
                 ctx.rp(Svn::Core::PROP_REVISION_LOG,
                        @repos_uri, info.revision))
    assert_equal([
                   {
                     Svn::Core::PROP_REVISION_AUTHOR => @author,
                     Svn::Core::PROP_REVISION_DATE => info.date,
                     Svn::Core::PROP_REVISION_LOG => new_log,
                   },
                   info.revision
                 ],
                 ctx.rplist(@repos_uri, info.revision))
    
    assert_equal(info.revision,
                 ctx.revprop_del(Svn::Core::PROP_REVISION_LOG,
                                 @repos_uri, info.revision))
    assert_equal([nil, info.revision],
                 ctx.rpg(Svn::Core::PROP_REVISION_LOG,
                         @repos_uri, info.revision))
    assert_equal(nil,
                 ctx.rp(Svn::Core::PROP_REVISION_LOG,
                        @repos_uri, info.revision))

    assert_equal(info.revision,
                 ctx.rpset(Svn::Core::PROP_REVISION_LOG, new_log,
                           @repos_uri, info.revision))
    assert_equal(new_log,
                 ctx.rp(Svn::Core::PROP_REVISION_LOG,
                        @repos_uri, info.revision))
    assert_equal(info.revision,
                 ctx.rps(Svn::Core::PROP_REVISION_LOG, nil,
                         @repos_uri, info.revision))
    assert_equal(nil,
                 ctx.rp(Svn::Core::PROP_REVISION_LOG,
                        @repos_uri, info.revision))
    
    assert_equal([
                   {
                     Svn::Core::PROP_REVISION_AUTHOR => @author,
                     Svn::Core::PROP_REVISION_DATE => info.date,
                   },
                   info.revision
                 ],
                 ctx.rpl(@repos_uri, info.revision))
  end
  
  def test_export
    log = "sample log"
    src = "source\n"
    file = "sample.txt"
    dir = "sample"
    dir_path = File.join(@wc_path, dir)
    path = File.join(dir_path, file)
    tmp_base_path = File.join(@tmp_path, "tmp")
    tmp_dir_path = File.join(tmp_base_path, dir)
    tmp_path = File.join(tmp_dir_path, file)

    ctx = make_context(log)

    ctx.mkdir(dir_path)
    File.open(path, "w") {|f| f.print(src)}
    ctx.add(path)
    rev = ctx.ci(@wc_path).revision

    assert_equal(rev, ctx.export(@repos_uri, tmp_base_path))
    assert_equal(src, File.open(tmp_path) {|f| f.read})
  end

  def test_ls
    log = "sample log"
    src = "source\n"
    file = "sample.txt"
    dir = "sample"
    dir_path = File.join(@wc_path, dir)
    path = File.join(@wc_path, file)

    ctx = make_context(log)

    ctx.mkdir(dir_path)
    File.open(path, "w") {|f| f.print(src)}
    ctx.add(path)
    rev = ctx.ci(@wc_path).revision

    dirents, locks = ctx.ls(@wc_path, rev)
    assert_equal([dir, file].sort, dirents.keys.sort)
    dir_dirent = dirents[dir]
    assert(dir_dirent.directory?)
    file_dirent = dirents[file]
    assert(file_dirent.file?)
  end

  def test_list
    log = "sample log"
    src = "source\n"
    file = "sample.txt"
    dir = "sample"
    prop_name = "sample-prop"
    prop_value = "sample value"
    dir_path = File.join(@wc_path, dir)
    path = File.join(@wc_path, file)

    ctx = make_context(log)

    ctx.mkdir(dir_path)
    File.open(path, "w") {|f| f.print(src)}
    ctx.add(path)
    ctx.prop_set(prop_name, prop_value, path)
    rev = ctx.ci(@wc_path).revision

    entries = []
    ctx.list(@wc_path, rev) do |path, dirent, lock, abs_path|
      entries << [path, dirent, lock, abs_path]
    end
    paths = entries.collect do |path, dirent, lock, abs_path|
      [path, abs_path]
    end
    assert_equal([["", "/"], [dir, "/"], [file, "/"]].sort, paths.sort)
    entries.each do |path, dirent, lock, abs_path|
      case path
      when dir, ""
        assert(dirent.directory?)
        assert_false(dirent.have_props?)
      when file
        assert(dirent.file?)
        assert_true(dirent.have_props?)
      else
        flunk
      end
    end
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
    ctx.set_notify_func do |notify|
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
    return unless Svn::Core.respond_to?(:add_windows_simple_provider)

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

  def test_add_providers
    ctx = Svn::Client::Context.new
    assert_nothing_raised do
      ctx.add_ssl_client_cert_file_provider
      ctx.add_ssl_client_cert_pw_file_provider
      ctx.add_ssl_server_trust_file_provider
    end
  end

  def test_not_new
    assert_raise(NoMethodError) do
      Svn::Client::CommitItem.new
    end
  end

  def test_log_msg_func_cancel
    log = "sample log"
    dir = "dir"
    dir_path = File.join(@wc_path, dir)

    ctx = make_context(log)
    ctx.set_log_msg_func do |items|
      raise Svn::Error::CANCELLED
    end
    ctx.mkdir(dir_path)
    assert_raise(Svn::Error::CANCELLED) do
      ctx.commit(@wc_path)
    end
  end
end
