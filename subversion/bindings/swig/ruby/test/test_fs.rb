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
    fs_type = Svn::Fs::TYPE_BDB
    config = {Svn::Fs::CONFIG_FS_TYPE => fs_type}

    assert(!File.exist?(path))
    fs = Svn::Fs::FileSystem.create(path, config)
    assert(File.exist?(path))
    assert_equal(fs_type, Svn::Fs.type(path))
    fs.set_warning_func do |err|
      p err
      abort
    end
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

  def test_root
    log = "sample log"
    file = "sample.txt"
    src = "sample source"
    path_in_repos = "/#{file}"
    path = File.join(@wc_path, file)
    
    assert_nil(@fs.root.name)
    
    ctx = make_context(log)
    FileUtils.touch(path)
    ctx.add(path)
    rev1 = ctx.commit(@wc_path).revision
    file_id1 = @fs.root.node_id(path_in_repos)

    assert_equal(rev1, @fs.root.revision)
    assert_equal(Svn::Core::NODE_FILE, @fs.root.check_path(path_in_repos))
    assert(@fs.root.file?(path_in_repos))
    assert(!@fs.root.dir?(path_in_repos))
    
    assert_equal([path_in_repos], @fs.root.paths_changed.keys)
    info = @fs.root.paths_changed[path_in_repos]
    assert(info.text_mod?)
    assert(info.add?)
    
    File.open(path, "w") {|f| f.print(src)}
    rev2 = ctx.commit(@wc_path).revision
    file_id2 = @fs.root.node_id(path_in_repos)

    assert_equal(src, @fs.root.file_contents(path_in_repos){|f| f.read})
    assert_equal(src.length, @fs.root.file_length(path_in_repos))
    assert_equal(MD5.new(src).hexdigest,
                 @fs.root.file_md5_checksum(path_in_repos))

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

    assert_raises(Svn::Error::FS_NOT_TXN_ROOT) do
      @fs.root.set_node_prop(path_in_repos, "name", "value")
    end
  end

  def test_transaction
    log = "sample log"
    file = "sample.txt"
    src = "sample source"
    path_in_repos = "/#{file}"
    path = File.join(@wc_path, file)
    prop_name = "prop"
    prop_value = "value"

    ctx = make_context(log)
    File.open(path, "w") {|f| f.print(src)}
    ctx.add(path)
    ctx.commit(@wc_path)
    
    assert_raises(Svn::Error::FS_NO_SUCH_TRANSACTION) do
      @fs.open_txn("NOT-EXIST")
    end
    
    txn1 = @fs.transaction
    assert_equal([Svn::Core::PROP_REVISION_DATE], txn1.proplist.keys)
    assert_instance_of(Time, txn1.proplist[Svn::Core::PROP_REVISION_DATE])
    date = txn1.prop(Svn::Core::PROP_REVISION_DATE)
    assert_operator(date, :>=, Time.now - 1)
    assert_operator(date, :<=, Time.now + 1)
    txn1.set_prop(Svn::Core::PROP_REVISION_DATE, nil)
    assert_equal([], txn1.proplist.keys)
    assert_equal(youngest_rev, txn1.base_revision)
    assert(txn1.root.txn_root?)
    assert(!txn1.root.revision_root?)
    assert_equal(txn1.name, txn1.root.name)
    
    @fs.transaction do |txn|
      assert_nothing_raised do
        @fs.open_txn(txn.name)
      end
      txn2 = txn
    end
    
    txn3 = @fs.transaction
    
    assert_equal([txn1.name, txn3.name].sort, @fs.transactions.sort)
    @fs.purge_txn(txn3.name)
    assert_equal([txn1.name].sort, @fs.transactions.sort)
    
    @fs.transaction do |txn|
      assert(@fs.transactions.include?(txn.name))
      txn.abort
      assert(!@fs.transactions.include?(txn.name))
    end

    txn4 = @fs.transaction
    assert_equal({}, txn1.root.node_proplist(path_in_repos))
    assert_nil(txn1.root.node_prop(path_in_repos, prop_name))
    txn1.root.set_node_prop(path_in_repos, prop_name, prop_value)
    assert_equal(prop_value, txn1.root.node_prop(path_in_repos, prop_name))
    assert_equal({prop_name => prop_value},
                 txn1.root.node_proplist(path_in_repos))
    assert(txn1.root.props_changed?(path_in_repos, txn4.root, path_in_repos))
    assert(!txn1.root.props_changed?(path_in_repos, txn1.root, path_in_repos))
    txn1.root.set_node_prop(path_in_repos, prop_name, nil)
    assert_nil(txn1.root.node_prop(path_in_repos, prop_name))
    assert_equal({}, txn1.root.node_proplist(path_in_repos))
  end

  def test_operation
    log = "sample log"
    file = "sample.txt"
    file2 = "sample2.txt"
    file3 = "sample3.txt"
    dir = "sample"
    src = "sample source"
    path_in_repos = "/#{file}"
    path2_in_repos = "/#{file2}"
    path3_in_repos = "/#{file3}"
    dir_path_in_repos = "/#{dir}"
    path = File.join(@wc_path, file)
    path2 = File.join(@wc_path, file2)
    path3 = File.join(@wc_path, file3)
    dir_path = File.join(@wc_path, dir)
    token = @fs.generate_lock_token
    ctx = make_context(log)

    @fs.transaction do |txn|
      txn.root.make_file(file)
      txn.root.make_dir(dir)
    end
    ctx.up(@wc_path)
    assert(File.exist?(path))
    assert(File.directory?(dir_path))

    @fs.transaction do |txn|
      txn.root.copy(file2, @fs.root, file)
      txn.root.delete(file)
      txn.abort
    end
    ctx.up(@wc_path)
    assert(File.exist?(path))
    assert(!File.exist?(path2))
    
    @fs.transaction do |txn|
      txn.root.copy(file2, @fs.root, file)
      txn.root.delete(file)
    end
    ctx.up(@wc_path)
    assert(!File.exist?(path))
    assert(File.exist?(path2))

    prev_root = @fs.root(youngest_rev - 1)
    assert(!prev_root.contents_changed?(file, @fs.root, file2))
    File.open(path2, "w") {|f| f.print(src)}
    ctx.ci(@wc_path)
    assert(prev_root.contents_changed?(file, @fs.root, file2))
    
    txn1 = @fs.transaction
    access = Svn::Fs::Access.new(@author)
    @fs.access = access
    @fs.access.add_lock_token(token)
    assert_equal([], @fs.get_locks(file2))
    lock = @fs.lock(file2)
    assert_equal(lock.token, @fs.get_lock(file2).token)
    assert_equal([lock.token],
                 @fs.get_locks(file2).collect{|l| l.token})
    @fs.unlock(file2, lock.token)
    assert_equal([], @fs.get_locks(file2))

    entries = @fs.root.dir_entries("/")
    assert_equal([file2, dir].sort, entries.keys.sort)
    assert_equal(@fs.root.node_id(path2_in_repos).to_s,
                 entries[file2].id.to_s)
    assert_equal(@fs.root.node_id(dir_path_in_repos).to_s,
                 entries[dir].id.to_s)

    @fs.transaction do |txn|
      prev_root = @fs.root(youngest_rev - 2)
      txn.root.revision_link(prev_root, file)
    end
    ctx.up(@wc_path)
    assert(File.exist?(path))

    closest_root, closet_path = @fs.root.closest_copy(file2)
    assert_equal(path2_in_repos, closet_path)
  end

  def test_delta
    log = "sample log"
    file = "source.txt"
    src = "a\nb\nc\nd\ne\n"
    modified = "A\nb\nc\nd\nE\n"
    result = "a\n\n\n\ne\n"
    expected = "A\n\n\n\nE\n"
    path_in_repos = "/#{file}"
    path = File.join(@wc_path, file)
    
    ctx = make_context(log)
    
    File.open(path, "w") {|f| f.print(src)}
    ctx.add(path)
    rev1 = ctx.ci(@wc_path).revision

    File.open(path, "w") {|f| f.print(modified)}
    @fs.transaction do |txn|
      checksum = MD5.new(result).hexdigest
      stream = txn.root.apply_text(path_in_repos, checksum)
      stream.write(result)
      stream.close
    end
    ctx.up(@wc_path)
    assert_equal(expected, File.open(path){|f| f.read})

    rev2 = ctx.ci(@wc_path).revision
    stream = @fs.root(rev2).file_delta_stream(@fs.root(rev1),
                                              path_in_repos,
                                              path_in_repos)
    data = ''
    stream.each{|w| data << w.new_data}
    assert_equal(expected, data)

    File.open(path, "w") {|f| f.print(src)}
    rev3 = ctx.ci(@wc_path).revision
    
    File.open(path, "w") {|f| f.print(modified)}
    @fs.transaction do |txn|
      base_checksum = MD5.new(src).hexdigest
      checksum = MD5.new(result).hexdigest
      handler = txn.root.apply_textdelta(path_in_repos,
                                         base_checksum, checksum)
      assert_raises(Svn::Error::CHECKSUM_MISMATCH) do
        handler.call(nil)
      end
    end
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

    assert_equal(@author, @fs.prop(Svn::Core::PROP_REVISION_AUTHOR))
    assert_equal(log, @fs.prop(Svn::Core::PROP_REVISION_LOG))
    assert_equal([
                   Svn::Core::PROP_REVISION_AUTHOR,
                   Svn::Core::PROP_REVISION_DATE,
                   Svn::Core::PROP_REVISION_LOG,
                 ].sort,
                 @fs.proplist.keys.sort)
    @fs.set_prop(Svn::Core::PROP_REVISION_LOG, nil)
    assert_nil(@fs.prop(Svn::Core::PROP_REVISION_LOG))
    assert_equal([
                   Svn::Core::PROP_REVISION_AUTHOR,
                   Svn::Core::PROP_REVISION_DATE,
                 ].sort,
                 @fs.proplist.keys.sort)
  end

end
