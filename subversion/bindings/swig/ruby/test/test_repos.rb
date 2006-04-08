require "tempfile"

require "my-assertions"
require "util"

require "svn/core"
require "svn/fs"
require "svn/repos"
require "svn/client"

class SvnReposTest < Test::Unit::TestCase
  include SvnTestUtil
  
  def setup
    setup_basic
  end

  def teardown
    teardown_basic
  end

  def test_version
    assert_equal(Svn::Core.subr_version, Svn::Repos.version)
  end

  def test_path
    assert_equal(@repos_path, @repos.path)

    assert_equal(File.join(@repos_path, "db"), @repos.db_env)

    assert_equal(File.join(@repos_path, "conf"), @repos.conf_dir)
    assert_equal(File.join(@repos_path, "conf", "svnserve.conf"),
                 @repos.svnserve_conf)
    
    locks_dir = File.join(@repos_path, "locks")
    assert_equal(locks_dir, @repos.lock_dir)
    assert_equal(File.join(locks_dir, "db.lock"),
                 @repos.db_lockfile)
    assert_equal(File.join(locks_dir, "db-logs.lock"),
                 @repos.db_logs_lockfile)

    hooks_dir = File.join(@repos_path, "hooks")
    assert_equal(hooks_dir, @repos.hook_dir)
    
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

    assert_equal(File.join(hooks_dir, "pre-lock"),
                 @repos.pre_lock_hook)
    assert_equal(File.join(hooks_dir, "post-lock"),
                 @repos.post_lock_hook)

    assert_equal(File.join(hooks_dir, "pre-unlock"),
                 @repos.pre_unlock_hook)
    assert_equal(File.join(hooks_dir, "post-unlock"),
                 @repos.post_unlock_hook)

    
    search_path = @repos_path
    assert_equal(@repos_path, Svn::Repos.find_root_path(search_path))
    search_path = "#{@repos_path}/XXX"
    assert_equal(@repos_path, Svn::Repos.find_root_path(search_path))

    search_path = "not-found"
    assert_equal(nil, Svn::Repos.find_root_path(search_path))
  end

  def test_create
    tmp_repos_path = File.join(@tmp_path, "repos")
    fs_config = {Svn::Fs::CONFIG_FS_TYPE => Svn::Fs::TYPE_BDB}
    repos = Svn::Repos.create(tmp_repos_path, {}, fs_config)
    assert(File.exist?(tmp_repos_path))
    fs_type_path = File.join(repos.fs.path, Svn::Fs::CONFIG_FS_TYPE)
    assert_equal(Svn::Fs::TYPE_BDB,
                 File.open(fs_type_path) {|f| f.read.chop})
    repos.fs.set_warning_func(&warning_func)
    Svn::Repos.delete(tmp_repos_path)
    assert(!File.exist?(tmp_repos_path))
  end

  def test_logs
    log1 = "sample log1"
    log2 = "sample log2"
    log3 = "sample log3"
    file = "file"
    src = "source"
    path = File.join(@wc_path, file)

    ctx = make_context(log1)
    File.open(path, "w") {|f| f.print(src)}
    ctx.add(path)
    info1 = ctx.ci(@wc_path)
    start_rev = info1.revision

    ctx = make_context(log2)
    File.open(path, "a") {|f| f.print(src)}
    info2 = ctx.ci(@wc_path)

    ctx = make_context(log3)
    File.open(path, "a") {|f| f.print(src)}
    info3 = ctx.ci(@wc_path)
    end_rev = info3.revision

    logs = @repos.logs(file, start_rev, end_rev, end_rev - start_rev + 1)
    logs = logs.collect do |changed_paths, revision, author, date, message|
      paths = {}
      changed_paths.each do |key, changed_path|
        paths[key] = changed_path.action
      end
      [paths, revision, author, date, message]
    end
    assert_equal([
                   [
                     {"/#{file}" => "A"},
                     info1.revision,
                     @author,
                     info1.date,
                     log1,
                   ],
                   [
                     {"/#{file}" => "M"},
                     info2.revision,
                     @author,
                     info2.date,
                     log2,
                   ],
                   [
                     {"/#{file}" => "M"},
                     info3.revision,
                     @author,
                     info3.date,
                     log3,
                   ],
                 ],
                 logs)
    revs = []
    @repos.file_revs(file, start_rev, end_rev) do |path, rev, *rest|
      revs << [path, rev]
    end
    assert_equal([
                   ["/#{file}", info1.revision],
                   ["/#{file}", info2.revision],
                   ["/#{file}", info3.revision],
                 ],
                 revs)


    rev, date, author = @repos.fs.root.committed_info("/")
    assert_equal(info3.revision, rev)
    assert_equal(info3.date, date)
    assert_equal(info3.author, author)
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
    fs_config = {}

    repos = Svn::Repos.create(dest_path, config, fs_config)
    repos.fs.set_warning_func(&warning_func)

    FileUtils.mv(@repos.path, backup_path)
    FileUtils.mv(repos.path, @repos.path)

    assert_raises(Svn::Error::FS_NO_SUCH_REVISION) do
      assert_equal(log, ctx.log_message(path, rev))
    end

    FileUtils.rm_r(@repos.path)
    Svn::Repos.hotcopy(backup_path, @repos.path)
    assert_equal(log, ctx.log_message(path, rev))
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

    prev_rev = @repos.youngest_rev
    @repos.transaction_for_update(@author) do |txn|
    end
    assert_equal(prev_rev, @repos.youngest_rev)
  end

  def test_trace_node_locations
    file1 = "file1"
    file2 = "file2"
    file3 = "file3"
    path1 = File.join(@wc_path, file1)
    path2 = File.join(@wc_path, file2)
    path3 = File.join(@wc_path, file3)
    log = "sample log"
    ctx = make_context(log)

    FileUtils.touch(path1)
    ctx.add(path1)
    rev1 = ctx.ci(@wc_path).revision

    ctx.mv(path1, path2)
    rev2 = ctx.ci(@wc_path).revision
    
    ctx.cp(path2, path3)
    rev3 = ctx.ci(@wc_path).revision

    assert_equal({
                   rev1 => "/#{file1}",
                   rev2 => "/#{file2}",
                   rev3 => "/#{file2}",
                 },
                 @repos.fs.trace_node_locations("/#{file2}",
                                                [rev1, rev2, rev3]))
  end

  def test_report
    file = "file"
    file2 = "file2"
    fs_base = "base"
    path = File.join(@wc_path, file)
    path2 = File.join(@wc_path, file2)
    source = "sample source"
    log = "sample log"
    ctx = make_context(log)

    File.open(path, "w") {|f| f.print(source)}
    ctx.add(path)
    rev = ctx.ci(@wc_path).revision

    assert_equal(Svn::Core::NODE_FILE, @repos.fs.root.stat(file).kind)

    editor = TestEditor.new
    @repos.report(rev, @author, fs_base, "/", nil, editor) do |baton|
      baton.link_path(file, file2, rev)
      baton.delete_path(file)
    end
    assert_equal([
                   :set_target_revision,
                   :open_root,
                   :close_directory,
                   :close_edit,
                 ],
                 editor.sequence.collect{|meth, *args| meth})
  end

  def test_commit_editor
    trunk = "trunk"
    tags = "tags"
    tags_sub = "sub"
    file = "file"
    source = "sample source"
    trunk_dir_path = File.join(@wc_path, trunk)
    tags_dir_path = File.join(@wc_path, tags)
    tags_sub_dir_path = File.join(tags_dir_path, tags_sub)
    trunk_path = File.join(trunk_dir_path, file)
    tags_path = File.join(tags_dir_path, file)
    tags_sub_path = File.join(tags_sub_dir_path, file)
    trunk_repos_uri = "#{@repos_uri}/#{trunk}"
    rev1 = @repos.youngest_rev
    
    editor = @repos.commit_editor(@repos_uri, "/")
    root_baton = editor.open_root(rev1)
    dir_baton = editor.add_directory(trunk, root_baton, nil, rev1)
    file_baton = editor.add_file("#{trunk}/#{file}", dir_baton, nil, -1)
    ret = editor.apply_textdelta(file_baton, nil)
    ret.send(source)
    editor.close_edit
    
    assert_equal(rev1 + 1, @repos.youngest_rev)
    rev2 = @repos.youngest_rev
    
    ctx = make_context("")
    ctx.up(@wc_path)
    assert_equal(source, File.open(trunk_path) {|f| f.read})

    editor = @repos.commit_editor(@repos_uri, "/")
    root_baton = editor.open_root(rev2)
    dir_baton = editor.add_directory(tags, root_baton, nil, rev2)
    subdir_baton = editor.add_directory("#{tags}/#{tags_sub}",
                                        dir_baton,
                                        trunk_repos_uri,
                                        rev2)
    editor.close_edit
    
    assert_equal(rev2 + 1, @repos.youngest_rev)
    rev3 = @repos.youngest_rev
    
    ctx.up(@wc_path)
    assert_equal([
                   ["/#{tags}/#{tags_sub}/#{file}", rev3],
                   ["/#{trunk}/#{file}", rev2],
                 ],
                 @repos.fs.history("#{tags}/#{tags_sub}/#{file}",
                                   rev1, rev3, rev2))

    editor = @repos.commit_editor(@repos_uri, "/")
    root_baton = editor.open_root(rev3)
    dir_baton = editor.delete_entry(tags, rev3, root_baton)
    editor.close_edit

    ctx.up(@wc_path)
    assert(!File.exist?(tags_path))
  end

  def test_prop
    file = "file"
    path = File.join(@wc_path, file)
    source = "sample source"
    log = "sample log"
    ctx = make_context(log)

    File.open(path, "w") {|f| f.print(source)}
    ctx.add(path)
    ctx.ci(@wc_path)

    assert_equal([
                   Svn::Core::PROP_REVISION_AUTHOR,
                   Svn::Core::PROP_REVISION_LOG,
                   Svn::Core::PROP_REVISION_DATE,
                 ].sort,
                 @repos.proplist.keys.sort)
    assert_equal(log, @repos.prop(Svn::Core::PROP_REVISION_LOG))
    @repos.set_prop(@author, Svn::Core::PROP_REVISION_LOG, nil)
    assert_nil(@repos.prop(Svn::Core::PROP_REVISION_LOG))
    assert_equal([
                   Svn::Core::PROP_REVISION_AUTHOR,
                   Svn::Core::PROP_REVISION_DATE,
                 ].sort,
                 @repos.proplist.keys.sort)
  end
  
  def test_load
    file = "file"
    path = File.join(@wc_path, file)
    source = "sample source"
    log = "sample log"
    ctx = make_context(log)

    File.open(path, "w") {|f| f.print(source)}
    ctx.add(path)
    rev1 = ctx.ci(@wc_path).revision

    File.open(path, "a") {|f| f.print(source)}
    rev2 = ctx.ci(@wc_path).revision

    dest_path = File.join(@tmp_path, "dest")
    repos = Svn::Repos.create(dest_path)

    assert_not_equal(@repos.fs.root.committed_info("/"),
                     repos.fs.root.committed_info("/"))

    dump = Tempfile.new("dump")
    feedback = Tempfile.new("feedback")
    dump.open
    feedback.open
    @repos.dump_fs(dump, feedback, rev1, rev2)
    dump.close
    feedback.close
    dump.open
    feedback.open
    repos.load_fs(dump, feedback, Svn::Repos::LOAD_UUID_DEFAULT, "/")

    assert_equal(@repos.fs.root.committed_info("/"),
                 repos.fs.root.committed_info("/"))
  end

  def test_node_editor
    file = "file"
    dir1 = "dir1"
    dir2 = "dir2"
    dir3 = "dir3"
    dir1_path = File.join(@wc_path, dir1)
    dir2_path = File.join(dir1_path, dir2)
    dir3_path = File.join(dir2_path, dir3)
    path = File.join(dir3_path, file)
    source = "sample source"
    log = "sample log"
    
    ctx = make_context(log)
    FileUtils.mkdir_p(dir3_path)
    FileUtils.touch(path)
    ctx.add(dir1_path)
    rev1 = ctx.ci(@wc_path).revision

    ctx.rm(dir3_path)
    rev2 = ctx.ci(@wc_path).revision

    rev1_root = @repos.fs.root(rev1)
    rev2_root = @repos.fs.root(rev2)
    editor = @repos.node_editor(rev1_root, rev2_root)
    rev2_root.replay(editor)

    tree = editor.baton.node

    assert_equal("", tree.name)
    assert_equal(dir1, tree.child.name)
    assert_equal(dir2, tree.child.child.name)
  end

  def test_lock
    file = "file"
    log = "sample log"
    path = File.join(@wc_path, file)
    path_in_repos = "/#{file}"
    ctx = make_context(log)
    
    FileUtils.touch(path)
    ctx.add(path)
    rev = ctx.ci(@wc_path).revision
    
    access = Svn::Fs::Access.new(@author)
    @repos.fs.access = access
    lock = @repos.lock(file)
    locks = @repos.get_locks(file)
    assert_equal([path_in_repos], locks.keys)
    assert_equal(lock.token, locks[path_in_repos].token)
    @repos.unlock(file, lock.token)
    assert_equal({}, @repos.get_locks(file))
  end

  def test_authz
    name = "REPOS"
    conf_path = File.join(@tmp_path, "authz_file")
    File.open(conf_path, "w") do |f|
      f.print(<<-EOF)
[/]
#{@author} = r
EOF
    end
    
    authz = Svn::Repos::Authz.read(conf_path)
    assert(authz.can_access?(name, "/", @author, Svn::Repos::AUTHZ_READ))
    assert(!authz.can_access?(name, "/", @author, Svn::Repos::AUTHZ_WRITE))
    assert(!authz.can_access?(name, "/", "FOO", Svn::Repos::AUTHZ_READ))
  end

  def warning_func
    Proc.new do |err|
      STDERR.puts err if $DEBUG
    end
  end
  
  class TestEditor < Svn::Delta::BaseEditor
    attr_reader :sequence
    def initialize
      @sequence = []
    end
    
    def set_target_revision(target_revision)
      @sequence << [:set_target_revision, target_revision]
    end
    
    def open_root(base_revision)
      @sequence << [:open_root, base_revision]
    end
    
    def delete_entry(path, revision, parent_baton)
      @sequence << [:delete_entry, path, revision, parent_baton]
    end
    
    def add_directory(path, parent_baton,
                      copyfrom_path, copyfrom_revision)
      @sequence << [:add_directory, path, parent_baton,
        copyfrom_path, copyfrom_revision]
    end
    
    def open_directory(path, parent_baton, base_revision)
      @sequence << [:open_directory, path, parent_baton, base_revision]
    end
    
    def change_dir_prop(dir_baton, name, value)
      @sequence << [:change_dir_prop, dir_baton, name, value]
    end
    
    def close_directory(dir_baton)
      @sequence << [:close_directory, dir_baton]
    end
    
    def absent_directory(path, parent_baton)
      @sequence << [:absent_directory, path, parent_baton]
    end
    
    def add_file(path, parent_baton,
                 copyfrom_path, copyfrom_revision)
      @sequence << [:add_file, path, parent_baton,
        copyfrom_path, copyfrom_revision]
    end
    
    def open_file(path, parent_baton, base_revision)
      @sequence << [:open_file, path, parent_baton, base_revision]
    end
    
    # return nil or object which has `call' method.
    def apply_textdelta(file_baton, base_checksum)
      @sequence << [:apply_textdelta, file_baton, base_checksum]
      nil
    end
    
    def change_file_prop(file_baton, name, value)
      @sequence << [:change_file_prop, file_baton, name, value]
    end
    
    def close_file(file_baton, text_checksum)
      @sequence << [:close_file, file_baton, text_checksum]
    end
    
    def absent_file(path, parent_baton)
      @sequence << [:absent_file, path, parent_baton]
    end
    
    def close_edit(baton)
      @sequence << [:close_edit, baton]
    end
    
    def abort_edit(baton)
      @sequence << [:abort_edit, baton]
    end
  end
end
