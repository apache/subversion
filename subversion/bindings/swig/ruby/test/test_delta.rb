require "util"

require "svn/info"

class SvnDeltaTest < Test::Unit::TestCase
  include SvnTestUtil
  
  def setup
    setup_basic
  end

  def teardown
    teardown_basic
  end
  
  def test_changed
    dir = "changed_dir"
    tmp_dir1 = "changed_tmp_dir1"
    tmp_dir2 = "changed_tmp_dir2"
    tmp_dir3 = "changed_tmp_dir3"
    dir_path = File.join(@wc_path, dir)
    tmp_dir1_path = File.join(@wc_path, tmp_dir1)
    tmp_dir2_path = File.join(@wc_path, tmp_dir2)
    tmp_dir3_path = File.join(dir_path, tmp_dir3)
    dir_svn_path = dir
    tmp_dir1_svn_path = tmp_dir1
    tmp_dir2_svn_path = tmp_dir2
    tmp_dir3_svn_path = [dir_svn_path, tmp_dir3].join("/")

    log = "added 3 dirs\nanded 5 files"
    ctx = make_context(log)

    ctx.mkdir([dir_path, tmp_dir1_path, tmp_dir2_path])

    file1 = "changed1.txt"
    file2 = "changed2.txt"
    file3 = "changed3.txt"
    file4 = "changed4.txt"
    file5 = "changed5.txt"
    file1_path = File.join(@wc_path, file1)
    file2_path = File.join(dir_path, file2)
    file3_path = File.join(@wc_path, file3)
    file4_path = File.join(dir_path, file4)
    file5_path = File.join(@wc_path, file5)
    file1_svn_path = file1
    file2_svn_path = [dir_svn_path, file2].join("/")
    file3_svn_path = file3
    file4_svn_path = [dir_svn_path, file4].join("/")
    file5_svn_path = file5
    FileUtils.touch(file1_path)
    FileUtils.touch(file2_path)
    FileUtils.touch(file3_path)
    FileUtils.touch(file4_path)
    FileUtils.touch(file5_path)
    ctx.add(file1_path)
    ctx.add(file2_path)
    ctx.add(file3_path)
    ctx.add(file4_path)
    ctx.add(file5_path)

    commit_info = ctx.commit(@wc_path)
    first_rev = commit_info.revision

    editor = traverse(Svn::Delta::ChangedEditor, commit_info.revision, true)
    assert_equal([
                   file1_svn_path, file2_svn_path,
                   file3_svn_path, file4_svn_path,
                   file5_svn_path,
                 ].sort,
                 editor.added_files)
    assert_equal([], editor.updated_files)
    assert_equal([], editor.deleted_files)
    assert_equal([].sort, editor.updated_dirs)
    assert_equal([].sort, editor.deleted_dirs)
    assert_equal([
                   "#{dir_svn_path}/",
                   "#{tmp_dir1_svn_path}/",
                   "#{tmp_dir2_svn_path}/"
                 ].sort,
                 editor.added_dirs)

    
    log = "deleted 2 dirs\nchanged 3 files\ndeleted 2 files\nadded 3 files"
    ctx = make_context(log)
    
    file6 = "changed6.txt"
    file7 = "changed7.txt"
    file8 = "changed8.txt"
    file9 = "changed9.txt"
    file6_path = File.join(dir_path, file6)
    file7_path = File.join(@wc_path, file7)
    file8_path = File.join(dir_path, file8)
    file9_path = File.join(dir_path, file9)
    file6_svn_path = [dir_svn_path, file6].join("/")
    file7_svn_path = file7
    file8_svn_path = [dir_svn_path, file8].join("/")
    file9_svn_path = [dir_svn_path, file9].join("/")
    
    File.open(file1_path, "w") {|f| f.puts "changed"}
    File.open(file2_path, "w") {|f| f.puts "changed"}
    File.open(file3_path, "w") {|f| f.puts "changed"}
    ctx.rm_f([file4_path, file5_path])
    FileUtils.touch(file6_path)
    FileUtils.touch(file7_path)
    FileUtils.touch(file8_path)
    ctx.add(file6_path)
    ctx.add(file7_path)
    ctx.add(file8_path)
    ctx.cp(file1_path, file9_path)
    ctx.rm(tmp_dir1_path)
    ctx.mv(tmp_dir2_path, tmp_dir3_path)

    commit_info = ctx.commit(@wc_path)
    second_rev = commit_info.revision
    
    editor = traverse(Svn::Delta::ChangedEditor, commit_info.revision, true)
    assert_equal([file1_svn_path, file2_svn_path, file3_svn_path].sort,
                 editor.updated_files)
    assert_equal([file4_svn_path, file5_svn_path].sort,
                 editor.deleted_files)
    assert_equal([file6_svn_path, file7_svn_path, file8_svn_path].sort,
                 editor.added_files)
    assert_equal([].sort, editor.updated_dirs)
    assert_equal([
                   [file9_svn_path, file1_svn_path, first_rev]
                 ].sort_by{|x| x[0]},
                 editor.copied_files)
    assert_equal([
                   ["#{tmp_dir3_svn_path}/", "#{tmp_dir2_svn_path}/", first_rev]
                 ].sort_by{|x| x[0]},
                 editor.copied_dirs)
    assert_equal(["#{tmp_dir1_svn_path}/", "#{tmp_dir2_svn_path}/"].sort,
                 editor.deleted_dirs)
    assert_equal([].sort, editor.added_dirs)
  end

  def test_change_prop
    prop_name = "prop"
    prop_value = "value"
    
    dir = "dir"
    dir_path = File.join(@wc_path, dir)
    dir_svn_path = dir

    log = "added 1 dirs\nanded 2 files"
    ctx = make_context(log)

    ctx.mkdir([dir_path])

    file1 = "file1.txt"
    file2 = "file2.txt"
    file1_path = File.join(@wc_path, file1)
    file2_path = File.join(dir_path, file2)
    file1_svn_path = file1
    file2_svn_path = [dir_svn_path, file2].join("/")
    FileUtils.touch(file1_path)
    FileUtils.touch(file2_path)
    ctx.add(file1_path)
    ctx.add(file2_path)

    ctx.propset(prop_name, prop_value, dir_path)

    commit_info = ctx.commit(@wc_path)

    editor = traverse(Svn::Delta::ChangedDirsEditor, commit_info.revision)
    assert_equal(["", dir_svn_path].collect{|path| "#{path}/"}.sort,
                 editor.changed_dirs)

    
    log = "prop changed"
    ctx = make_context(log)
    
    ctx.propdel(prop_name, dir_path)

    commit_info = ctx.commit(@wc_path)

    editor = traverse(Svn::Delta::ChangedDirsEditor, commit_info.revision)
    assert_equal([dir_svn_path].collect{|path| "#{path}/"}.sort,
                 editor.changed_dirs)

    
    ctx.propset(prop_name, prop_value, file1_path)

    commit_info = ctx.commit(@wc_path)

    editor = traverse(Svn::Delta::ChangedDirsEditor, commit_info.revision)
    assert_equal([""].collect{|path| "#{path}/"}.sort,
                 editor.changed_dirs)


    ctx.propdel(prop_name, file1_path)
    ctx.propset(prop_name, prop_value, file2_path)

    commit_info = ctx.commit(@wc_path)

    editor = traverse(Svn::Delta::ChangedDirsEditor, commit_info.revision)
    assert_equal(["", dir_svn_path].collect{|path| "#{path}/"}.sort,
                 editor.changed_dirs)
  end

  def test_deep_copy
    dir1 = "dir1"
    dir2 = "dir2"
    dir1_path = File.join(@wc_path, dir1)
    dir2_path = File.join(dir1_path, dir2)
    dir1_svn_path = dir1
    dir2_svn_path = [dir1, dir2].join("/")

    log = "added 2 dirs\nanded 3 files"
    ctx = make_context(log)

    ctx.mkdir([dir1_path, dir2_path])
    
    file1 = "file1.txt"
    file2 = "file2.txt"
    file3 = "file3.txt"
    file1_path = File.join(@wc_path, file1)
    file2_path = File.join(dir1_path, file2)
    file3_path = File.join(dir2_path, file3)
    file1_svn_path = file1
    file2_svn_path = [dir1_svn_path, file2].join("/")
    file3_svn_path = [dir2_svn_path, file3].join("/")
    FileUtils.touch(file1_path)
    FileUtils.touch(file2_path)
    FileUtils.touch(file3_path)
    ctx.add(file1_path)
    ctx.add(file2_path)
    ctx.add(file3_path)

    commit_info = ctx.commit(@wc_path)
    first_rev = commit_info.revision

    editor = traverse(Svn::Delta::ChangedEditor, commit_info.revision, true)
    assert_equal([
                   file1_svn_path, file2_svn_path,
                   file3_svn_path,
                 ].sort,
                 editor.added_files)
    assert_equal([].sort, editor.updated_files)
    assert_equal([].sort, editor.deleted_files)
    assert_equal([].sort, editor.updated_dirs)
    assert_equal([].sort, editor.deleted_dirs)
    assert_equal([
                   "#{dir1_svn_path}/",
                   "#{dir2_svn_path}/",
                 ].sort,
                 editor.added_dirs)

    
    log = "copied top dir"
    ctx = make_context(log)

    dir3 = "dir3"
    dir3_path = File.join(@wc_path, dir3)
    dir3_svn_path = dir3
    
    ctx.cp(dir1_path, dir3_path)

    commit_info = ctx.commit(@wc_path)
    second_rev = commit_info.revision
    
    editor = traverse(Svn::Delta::ChangedEditor, commit_info.revision, true)
    assert_equal([].sort, editor.updated_files)
    assert_equal([].sort, editor.deleted_files)
    assert_equal([].sort, editor.added_files)
    assert_equal([].sort, editor.updated_dirs)
    assert_equal([].sort, editor.copied_files)
    assert_equal([
                   ["#{dir3_svn_path}/", "#{dir1_svn_path}/", first_rev]
                 ].sort_by{|x| x[0]},
                 editor.copied_dirs)
    assert_equal([].sort, editor.deleted_dirs)
    assert_equal([].sort, editor.added_dirs)
  end
  
  private
  def traverse(editor_class, rev, pass_root=false)
    root = @fs.root
    base_rev = rev - 1
    base_root = @fs.root(base_rev)
    if pass_root
      editor = editor_class.new(root, base_root)
    else
      editor = editor_class.new
    end
    base_root.editor = editor
    base_root.dir_delta("", "", root, "")
    editor
  end
end
