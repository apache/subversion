#!/usr/bin/env ruby

require "svn/core"
require "svn/fs"
require "svn/delta"
require "svn/repos"

def basename(path)
  path.chomp("/")
end

class SvnLook
  def initialize(pool, path, rev, txn)
    @pool = pool
    @fs = Svn::Repos.open(path, @pool).fs

    if txn
      @txn = @fs.open_txn(txn)
    else
      @txn = nil
      rev ||= @fs.youngest_rev
    end
    @rev = rev
  end

  def run(cmd, *args)
    dispatch(cmd, *args)
  end

  private
  def dispatch(cmd, *args)
    if respond_to?("cmd_#{cmd}", true)
      begin
        __send__("cmd_#{cmd}", *args)
      rescue ArgumentError
        puts $!.message
        puts $@
        puts("invalid argument for #{cmd}: #{args.join(' ')}")
      end
    else
      puts("unknown command: #{cmd}")
    end
  end

  def cmd_default
    cmd_info
    cmd_tree
  end

  def cmd_author
    puts(property(Svn::Core::PROP_REVISION_AUTHOR) || "")
  end

  def cmd_cat
  end

  def cmd_changed
    print_tree(ChangedEditor, nil, true)
  end
  
  def cmd_date
    if @txn
      puts
    else
      date = property(Svn::Core::PROP_REVISION_DATE)
      if date
        time = str_to_time(date)
        puts time.strftime('%Y-%m-%d %H:%M(%Z)')
      else
        puts
      end
    end
  end

  def cmd_diff
    print_tree(DiffEditor, nil, true)
  end
  
  def cmd_dirs_changed
    print_tree(DirsChangedEditor)
  end
  
  def cmd_ids
    print_tree(Editor, 0, true)
  end
  
  def cmd_info
    cmd_author
    cmd_date
    cmd_log(true)
  end

  def cmd_log(print_size=false)
    log = property(Svn::Core::PROP_REVISION_LOG) || ''
    puts log.length if print_size
    puts log
  end

  def cmd_tree
    print_tree(Editor, 0)
  end

  def property(name)
    if @txn
      @txn.prop(name)
    else
      @fs.prop(name, @rev)
    end
  end

  def print_tree(editor_class, base_rev=nil, pass_root=false)
    if base_rev.nil?
      if @txn
        base_rev = @txn.base_revision
      else
        base_rev = @rev - 1
      end
    end

    if @txn
      root = @txn.root
    else
      root = @fs.root(@rev)
    end

    base_root = @fs.root(base_rev)

    if pass_root
      editor = editor_class.new(root, base_root)
    else
      editor = editor_class.new
    end

    base_root.editor = editor
    base_root.dir_delta('', '', root, '')
  end

  def str_to_time(str)
    Svn::Util.string_to_time(str, @pool)
  end
  
  class Editor < Svn::Delta::Editor
    def initialize(root=nil, base_root=nil)
      @root = root
      # base_root ignored
      
      @indent = ""
    end
    
    def open_root(base_revision, dir_pool)
      puts "/#{id('/', dir_pool)}"
      @indent << ' '
    end

    def add_directory(path, *args)
      puts "#{@indent}#{basename(path)}/#{id(path, args[-1])}"
      @indent << ' '
    end

    alias open_directory add_directory

    def close_directory(baton)
      @indent.chop!
    end

    def add_file(path, *args)
      puts "#{@indent}#{basename(path)}#{id(path, args[-1])}"
    end
    
    alias open_file add_file

    private
    def id(path, pool)
      if @root
        fs_id = @root.node_id(path, pool)
        " <#{fs_id.unparse(pool)}>"
      else
        ""
      end
    end
  end

  class DirsChangedEditor < Svn::Delta::Editor
    def open_root(base_revision, dir_pool)
      [true, '']
    end

    def delete_entry(path, revision, parent_baton, pool)
      dir_changed(parent_baton)
    end

    def add_directory(path, parent_baton,
                      copyfrom_path, copyfrom_revision, dir_pool)
      dir_changed(parent_baton)
      [true, path]
    end

    def open_directory(path, parent_baton, base_revision, dir_pool)
      [true, path]
    end

    def change_dir_prop(dir_baton, name, value, pool)
      dir_changed(dir_baton)
    end

    def add_file(path, parent_baton,
                 copyfrom_path, copyfrom_revision, file_pool)
      dir_changed(parent_baton)
    end

    def open_file(path, parent_baton, base_revision, file_pool)
      dir_changed(parent_baton)
    end

    private
    def dir_changed( baton)
      if baton[0]
        # the directory hasn't been printed yet. do it.
        puts baton[1] + '/'
        baton[0] = nil
      end
    end
  end
    
  class ChangedEditor < Svn::Delta::Editor
    def initialize(root, base_root)
      @root = root
      @base_root = base_root
    end

    def open_root(base_revision, dir_pool)
      [true, '']
    end

    def delete_entry(path, revision, parent_baton, pool)
      print "D   #{path}"
      if @base_root.dir?('/' + path, pool)
        puts "/"
      else
        puts
      end
    end

    def add_directory(path, parent_baton,
                      copyfrom_path, copyfrom_revision, dir_pool)
      puts "A   #{path}/"
      [false, path]
    end

    def open_directory(path, parent_baton, base_revision, dir_pool)
      [true, path]
    end

    def change_dir_prop(dir_baton, name, value, pool)
      if dir_baton[0]
        # the directory hasn't been printed yet. do it.
        puts "_U  #{dir_baton[1]}/"
        dir_baton[0] = false
      end
    end

    def add_file(path, parent_baton,
                 copyfrom_path, copyfrom_revision, file_pool)
      puts "A   #{path}"
      ['_', ' ', nil]
    end

    def open_file(path, parent_baton, base_revision, file_pool)
      ['_', ' ', path]
    end

    def apply_textdelta(file_baton, base_checksum, pool)
      file_baton[0] = 'U'
      nil
    end

    def change_file_prop(file_baton, name, value, pool)
      file_baton[1] = 'U'
    end
    
    def close_file(file_baton, text_checksum)
      text_mod, prop_mod, path = file_baton
      # test the path. it will be None if we added this file.
      if path
        status = text_mod + prop_mod
        # was there some kind of change?
        if status != '_ '
          puts "#{status}  #{path}"
        end
      end
    end
  end
        
  class DiffEditor < Svn::Delta::Editor

    def initialize(root, base_root)
      @root = root
      @base_root = base_root
    end

    def delete_entry(path, revision, parent_baton, pool)
      unless @base_root.dir?('/' + path, pool)
        do_diff(path, nil, pool)
      end
    end

    def add_file(path, parent_baton,
                 copyfrom_path, copyfrom_revision, file_pool)
      do_diff(nil, path, file_pool)
      ['_', ' ', nil, file_pool]
    end

    def open_file(path, parent_baton, base_revision, file_pool)
      ['_', ' ', path, file_pool]
    end

    def apply_textdelta(file_baton, base_checksum, pool)
      if file_baton[2].nil?
        nil
      else
        do_diff(file_baton[2], file_baton[2], file_baton[3])
      end
    end

    private
    def do_diff(base_path, path, pool)
      if base_path.nil?
        puts("Added: #{path}")
        name = path
      elsif path.nil?
        puts("Removed: #{base_path}")
        name = base_path
      else
        puts "Modified: #{path}"
        name = path
      end
      
      base_label = "#{name} (original)"
      label = "#{name} (new)"
      differ = Svn::Fs::FileDiff.new(@base_root, base_path, @root, path, pool)
      
      puts "=" * 78
      puts differ.unified(base_label, label)
      puts
    end
  end
end

def usage
  messages = [
    "usage: #{$0} REPOS_PATH rev REV [COMMAND] - inspect revision REV",
    "       #{$0} REPOS_PATH txn TXN [COMMAND] - inspect transaction TXN",
    "       #{$0} REPOS_PATH [COMMAND] - inspect the youngest revision",
    "",
    "REV is a revision number > 0.",
    "TXN is a transaction name.",
    "",
    "If no command is given, the default output (which is the same as",
    "running the subcommands `info' then `tree') will be printed.",
    "",
    "COMMAND can be one of: ",
    "",
    "   author:        print author.",
    "   changed:       print full change summary: all dirs & files changed.",
    "   date:          print the timestamp (revisions only).",
    "   diff:          print GNU-style diffs of changed files and props.",
    "   dirs-changed:  print changed directories.",
    "   ids:           print the tree, with nodes ids.",
    "   info:          print the author, data, log_size, and log message.",
    "   log:           print log message.",
    "   tree:          print the tree.",
  ]
  puts(messages.join("\n"))
  exit(1)
end

if ARGV.empty?
  usage
end

path = ARGV.shift
cmd = ARGV.shift
rev = nil
txn = nil
case cmd
when "rev"
  rev = Integer(ARGV.shift)
  cmd = ARGV.shift
when "txn"
  txn = ARGV.shift
  cmd = ARGV.shift
end
cmd ||= "default"

Svn::Core::Pool.new do |pool|
  SvnLook.new(pool, path, rev, txn).run(cmd.gsub(/-/, '_'))
end
