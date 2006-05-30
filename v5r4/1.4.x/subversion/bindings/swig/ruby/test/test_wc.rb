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
    dir = "dir"
    file = "file"
    prop_name = "name"
    prop_value = "value"
    dir_path = File.join(@wc_path, dir)
    path = File.join(dir_path, file)
    ctx = make_context(log)

    ctx.mkdir(dir_path)
    File.open(path, "w") {|f| f.print(source)}
    ctx.add(path)
    rev = ctx.ci(@wc_path).revision

    result = Svn::Wc::AdmAccess.open_anchor(path, true, 5)
    anchor_access, target_access, target = result
    
    assert_equal(file, target)
    assert_equal(dir_path, anchor_access.path)
    assert_equal(dir_path, target_access.path)
    
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

    access = Svn::Wc::AdmAccess.probe_open(nil, @wc_path, false, 5)
    assert_equal(@wc_path, access.path)
    assert_equal(dir_path, access.retrieve(dir_path).path)
    assert_equal(dir_path, access.probe_retrieve(dir_path).path)
    assert_equal(dir_path, access.probe_try(dir_path, false, 5).path)

    Svn::Wc::AdmAccess.probe_open(nil, @wc_path, false, 5) do |access|
      assert(!access.locked?)
    end

    Svn::Wc::AdmAccess.probe_open(nil, @wc_path, true, 5) do |access|
      assert(access.locked?)
    end
  end

  def test_traversal_info
    info = Svn::Wc::TraversalInfo.new
    assert_equal([{}, {}], info.edited_externals)
  end

  def test_externals
    dir = "dir"
    url = "http://svn.example.com/trunk"
    description = "#{dir} #{url}"
    items = Svn::Wc.parse_externals_description(@wc_path, description)
    assert_equal([[dir, url]],
                 items.collect {|item| [item.target_dir, item.url]})
  end

  def test_notify
    path = @wc_path
    action = Svn::Wc::NOTIFY_SKIP
    notify = Svn::Wc::Notify.new(path, action)
    assert_equal(path, notify.path)
    assert_equal(action, notify.action)

    notify2 = notify.dup
    assert_equal(path, notify2.path)
    assert_equal(action, notify2.action)
  end

  def test_entry
    log = "sample log"
    source1 = "source"
    source2 = "SOURCE"
    source3 = "SHOYU"
    file = "file"
    path = File.join(@wc_path, file)
    ctx = make_context(log)

    File.open(path, "w") {|f| f.print(source1)}
    ctx.add(path)
    rev1 = ctx.ci(@wc_path).revision
    
    Svn::Wc::AdmAccess.open(nil, @wc_path, false, 5) do |access|
      show = true
      entry = Svn::Wc::Entry.new(path, access, show)
      assert_equal(file, entry.name)
      assert_equal(file, entry.dup.name)

      entries = access.read_entries
      assert_equal(["", file].sort, entries.keys.sort)
      entry = entries[file]
      assert_equal(file, entry.name)

      assert(!entry.conflicted?(@wc_path))
    end
    
    File.open(path, "w") {|f| f.print(source2)}
    rev2 = ctx.ci(@wc_path).revision

    ctx.up(@wc_path, rev1)
    File.open(path, "w") {|f| f.print(source3)}
    ctx.up(@wc_path, rev2)

    Svn::Wc::AdmAccess.open(nil, @wc_path, true, 5) do |access|
      entry = access.read_entries[file]
      assert(entry.conflicted?(@wc_path))
      assert(entry.text_conflicted?(@wc_path))
      assert(!entry.prop_conflicted?(@wc_path))

      access.resolved_conflict(path)
      assert(!entry.conflicted?(@wc_path))

      access.process_committed(@wc_path, rev2 + 1)
    end
  end

  def test_ancestry
    file1 = "file1"
    file2 = "file2"
    path1 = File.join(@wc_path, file1)
    path2 = File.join(@wc_path, file2)
    log = "sample log"
    ctx = make_context(log)

    FileUtils.touch(path1)
    ctx.add(path1)
    rev1 = ctx.ci(@wc_path).revision

    ctx.cp(path1, path2)
    rev2 = ctx.ci(@wc_path).revision

    Svn::Wc::AdmAccess.open(nil, @wc_path, false, 5) do |access|
      assert_equal(["#{@repos_uri}/#{file2}", rev2],
                   access.ancestry(path2))
      result = []
      callbacks = Proc.new do |path, entry|
        result << [path, entry]
      end
      def callbacks.found_entry(path, entry)
        call(path, entry)
      end
      access.walk_entries(@wc_path, callbacks)
      assert_equal([@wc_path, path1, path2].sort,
                   result.collect{|path, entry| path}.sort)
      entry = result.assoc(path2)[1]
      assert_equal(file2, entry.name)
    end

    Svn::Wc::AdmAccess.open(nil, @wc_path, true, 5) do |access|
      assert_raises(Svn::Error::WC_PATH_FOUND) do
        access.mark_missing_deleted(path1)
      end
      FileUtils.rm(path1)
      access.mark_missing_deleted(path1)
      access.maybe_set_repos_root(path2, @repos_uri)
    end

    Svn::Wc.ensure_adm(@wc_path, nil, @repos_uri, nil, 0)
  end

  def test_merge
    left = <<-EOL
a
b
c
d
e
EOL
    right = <<-EOR
A
b
c
d
E
EOR
    merge_target = <<-EOM
a
b
C
d
e
EOM
    expect = <<-EOE
A
b
C
d
E
EOE

    left_label = "left"
    right_label = "right"
    merge_target_label = "merge_target"

    left_file = "left"
    right_file = "right"
    merge_target_file = "merge_target"
    left_path = File.join(@wc_path, left_file)
    right_path = File.join(@wc_path, right_file)
    merge_target_path = File.join(@wc_path, merge_target_file)

    log = "sample log"
    ctx = make_context(log)

    File.open(left_path, "w") {|f| f.print(left)}
    File.open(right_path, "w") {|f| f.print(right)}
    File.open(merge_target_path, "w") {|f| f.print(merge_target)}
    ctx.add(left_path)
    ctx.add(right_path)
    ctx.add(merge_target_path)

    ctx.ci(@wc_path)

    Svn::Wc::AdmAccess.open(nil, @wc_path, true, 5) do |access|
      assert_equal(Svn::Wc::MERGE_MERGED,
                   access.merge(left_path, right_path, merge_target_path,
                                left_label, right_label, merge_target_label))
      assert_equal(expect, File.read(merge_target_path))
    end
  end

  def test_status
    source = "source"
    file1 = "file1"
    file2 = "file2"
    file3 = "file3"
    file4 = "file4"
    file5 = "file5"
    path1 = File.join(@wc_path, file1)
    path2 = File.join(@wc_path, file2)
    path3 = File.join(@wc_path, file3)
    path4 = File.join(@wc_path, file4)
    path5 = File.join(@wc_path, file5)
    log = "sample log"
    ctx = make_context(log)

    File.open(path1, "w") {|f| f.print(source)}
    ctx.add(path1)
    rev1 = ctx.ci(@wc_path).revision

    ctx.cp(path1, path2)
    rev2 = ctx.ci(@wc_path).revision

    FileUtils.rm(path1)
    
    Svn::Wc::AdmAccess.open(nil, @wc_path, false, 5) do |access|
      status = access.status(path1)
      assert_equal(Svn::Wc::STATUS_MISSING, status.text_status)
      assert_equal(Svn::Wc::STATUS_MISSING, status.dup.text_status)
    end

    ctx.revert(path1)
    
    Svn::Wc::AdmAccess.open(nil, @wc_path, true, 5) do |access|
      access.copy(path1, file3)
      assert(File.exist?(path3))
      access.delete(path1)
      assert(!File.exist?(path1))
      FileUtils.touch(path4)
      access.add(path4)
      assert(File.exist?(path4))
      access.add_repos_file2(path5, path2, {})
      assert_equal(source, File.open(path5) {|f| f.read})

      status = access.status(path2)
      assert_equal(Svn::Wc::STATUS_MISSING, status.text_status)
      access.remove_from_revision_control(file2)
      status = access.status(path2)
      assert_equal(Svn::Wc::STATUS_NONE, status.text_status)
    end
  end
  
  def test_locked
    log = "sample log"

    assert(!Svn::Wc.locked?(@wc_path))
    ctx = make_context(log)

    gc_disable do
      assert_raise(Svn::Error::FS_NO_SUCH_REVISION) do
        ctx.update(@wc_path, youngest_rev + 1)
      end
      assert(Svn::Wc.locked?(@wc_path))
    end
    gc_enable do
      GC.start
      assert(!Svn::Wc.locked?(@wc_path))
    end
    
    gc_disable do
      assert_raise(Svn::Error::FS_NO_SUCH_REVISION) do
        ctx.update(@wc_path, youngest_rev + 1)
      end
      assert(Svn::Wc.locked?(@wc_path))
      ctx.cleanup(@wc_path)
      assert(!Svn::Wc.locked?(@wc_path))
    end
  end

  def test_translated_file
    src_file = "src"
    crlf_file = "crlf"
    cr_file = "cr"
    lf_file = "lf"
    src_path = File.join(@wc_path, src_file)
    crlf_path = File.join(@wc_path, crlf_file)
    cr_path = File.join(@wc_path, cr_file)
    lf_path = File.join(@wc_path, lf_file)

    source = "a\n"
    crlf_source = source.gsub(/\n/, "\r\n")
    cr_source = source.gsub(/\n/, "\r")
    lf_source = source.gsub(/\n/, "\n")

    File.open(crlf_path, "w") {}
    File.open(cr_path, "w") {}
    File.open(lf_path, "w") {}

    log = "log"
    ctx = make_context(log)
    ctx.add(crlf_path)
    ctx.add(cr_path)
    ctx.add(lf_path)
    ctx.prop_set("svn:eol-style", "CRLF", crlf_path)
    ctx.prop_set("svn:eol-style", "CR", cr_path)
    ctx.prop_set("svn:eol-style", "LF", lf_path)
    ctx.ci(crlf_path)
    ctx.ci(cr_path)
    ctx.ci(lf_path)

    Svn::Wc::AdmAccess.open(nil, @wc_path, true, 5) do |access|
      File.open(src_path, "wb") {|f| f.print(source)}
      translated_file = access.translated_file(src_path, lf_path,
                                               Svn::Wc::TRANSLATE_TO_NF)
      File.open(src_path, "wb") {|f| f.print(File.read(translated_file))}

      translated_file = access.translated_file(src_path, crlf_path,
                                               Svn::Wc::TRANSLATE_FROM_NF)
      assert_equal(crlf_source, File.read(translated_file))
      translated_file = access.translated_file(src_path, cr_path,
                                               Svn::Wc::TRANSLATE_FROM_NF)
      assert_equal(cr_source, File.read(translated_file))
      translated_file = access.translated_file(src_path, lf_path,
                                               Svn::Wc::TRANSLATE_FROM_NF)
      assert_equal(lf_source, File.read(translated_file))
    end
  end

  def test_revision_status
    log = "log"
    file = "file"
    path = File.join(@wc_path, file)

    File.open(path, "w") {|f| f.print("a")}
    ctx = make_context(log)
    ctx.add(path)
    ctx.ci(path)

    File.open(path, "w") {|f| f.print("b")}
    ctx.ci(path)

    File.open(path, "w") {|f| f.print("c")}
    rev = ctx.ci(path).revision

    status = Svn::Wc::RevisionStatus.new(path, nil, true)
    assert_equal(rev, status.min_rev)
    assert_equal(rev, status.max_rev)
  end
end
