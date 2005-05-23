#!/usr/bin/env ruby

require "readline"
require "shellwords"
require "time"

require "svn/fs"
require "svn/core"
require "svn/repos"

class SvnShell

  WORDS = []

  class << self
    def method_added(name)
      if /^do_(.*)$/ =~ name.to_s
        WORDS << $1
      end
    end
  end
  
  def initialize(pool, path)
    @pool = pool
    @repos_path = path
    @in_rev_mode = true
    Svn::Core::Pool.new(@pool) do |tmp_pool|
      @rev = Svn::Repos.open(@repos_path, tmp_pool).fs.youngest_rev
    end
    @txn = nil
    @path = "/"
    @exited = false
  end

  def run
    while !@exited and buf = Readline.readline(prompt, true)
      cmd, *args = Shellwords.shellwords(buf)
      next if /\A\s*\z/ =~ cmd
      Svn::Core::Pool.new(@pool) do |pool|
        @fs = Svn::Repos.open(@repos_path, pool).fs
        if @in_rev_mode
          @root = @fs.root(@rev)
        else
          @root = @fs.open_txn(name).root
        end
        dispatch(cmd, *args)
        @root.close
      end
    end
  end

  private
  def prompt
    if @in_rev_mode
      mode = "rev"
      info = @rev
    else
      mode = "txn"
      info = @txn
    end
    "<#{mode}: #{info} #{@path}>$ "
  end
  
  def dispatch(cmd, *args)
    if respond_to?("do_#{cmd}", true)
      begin
        __send__("do_#{cmd}", *args)
      rescue ArgumentError
        # puts $!.message
        # puts $@
        puts("invalid argument for #{cmd}: #{args.join(' ')}")
      end
    else
      puts("unknown command: #{cmd}")
    end
  end

  def do_cat(path)
    new_path = parse_path(path)
    case @root.check_path(new_path)
    when Svn::Core::NODE_NONE
      puts "Path '#{new_path}' does not exist."
    when Svn::Core::NODE_DIR
      puts "Path '#{new_path}' is not a file."
    else
      @root.file_contents(new_path) do |stream|
        puts stream.read(@root.file_length(new_path))
      end
    end
  end

  def do_cd(path="/")
    new_path = parse_path(path)
    if @root.check_path(new_path) == Svn::Core::NODE_DIR
      @path = new_path
    else
      puts "Path '#{new_path}' is not a valid filesystem directory."
    end
  end
  
  def do_ls(*paths)
    paths << @path if paths.empty?
    paths.each do |path|
      new_path = parse_path(path)
      case @root.check_path(new_path)
      when Svn::Core::NODE_DIR
        parent = new_path
        entries = @root.dir_entries(parent)
      when Svn::Core::NODE_FILE
        parts = path_to_parts(new_path)
        name = parts.pop
        parent = parts_to_path(parts)
        puts "#{parent}:#{name}"
        tmp = @root.dir_entries(parent)
        if tmp[name].nil?
          return
        else
          entries = {name => tmp[name]}
        end
      else
        puts "Path '#{new_path}' not found."
        return
      end

      puts "   REV   AUTHOR  NODE-REV-ID     SIZE              DATE NAME"
      puts "-" * 76

      entries.keys.sort.each do |entry|
        fullpath = parent + '/' + entry
        size = ''
        if @root.dir?(fullpath)
          name = entry + '/'
        else
          size = @root.file_length(fullpath).to_i.to_s
          name = entry
        end
        
        node_id = entries[entry].id.to_s
        created_rev = @root.node_created_rev(fullpath)
        author = @fs.prop(Svn::Core::PROP_REVISION_AUTHOR, created_rev).to_s
        date = @fs.prop(Svn::Core::PROP_REVISION_DATE, created_rev)
        args = [
          created_rev, author[0,8],
          node_id, size, format_date(date), name
        ]
        puts "%6s %8s <%10s> %8s %17s %s" % args
      end
    end
  end

  def do_lstxns
    txns = @fs.list_transactions
    txns.sort
    counter = 0
    txns.each do |txn|
      counter = counter + 1
      puts "%8s  " % txn
      if counter == 6
        puts
        counter = 0
      end
    end
    puts
  end
  
  def do_pcat(path=nil)
    catpath = path || @path
    if @root.check_path(catpath) == Svn::Core::NODE_NONE
      puts "Path '#{catpath}' does not exist."
      return
    end

    plist = @root.node_proplist(catpath)
    return if plist.nil?

    plist.each do |key, value|
      puts "K #{key.size}"
      puts key
      puts "P #{value.size}"
      puts value
    end
    puts 'PROPS-END'
  end
      
  def do_setrev(rev)
    begin
      @fs.root(Integer(rev)).close
    rescue Svn::Error
      puts "Error setting the revision to '#{rev}': #{$!.message}"
      return
    end
    @rev = Integer(rev)
    @in_rev_mode = true
    path_landing
  end
  
  def do_settxn(name)
    new_root = nil
    begin
      txn = @fs.open_txn(name)
      txn.root.close
    rescue Svn::Error
      puts "Error setting the transaction to '#{name}': #{$!.message}"
      return
    end
    @txn = name
    @in_rev_mode = false
    path_landing
  end

  def do_youngest
    rev = @fs.youngest_rev
    puts rev
  end
  
  def do_exit
    @exited = true
  end

  def path_to_parts(path)
    path.split(/\/+/)
  end

  def parts_to_path(parts)
    normalized_parts = parts.reject{|part| part.empty?}
    "/#{normalized_parts.join('/')}"
  end
  
  def parse_path(path)
    if path[0,1] != "/" and @path != "/"
      path = "#{@path}/#{path}"
    end
    parts = path_to_parts(path)
    
    normalized_parts = []
    parts.each do |part|
      case part
      when "."
        # ignore
      when ".."
        normalized_parts.pop
      else
        normalized_parts << part
      end
    end
    parts_to_path(normalized_parts)
  end
  
  def path_landing
    found = false
    new_path = @path
    until found
      case @root.check_path(new_path)
      when Svn::Core::NODE_DIR
        found = true
      else
        parts = path_to_parts(new_path)
        parts.pop
        new_path = parts_to_path(parts)
      end
    end
    @path = new_path
  end

  def format_date(date_str)
    date = Svn::Util.string_to_time(date_str, @taskpool)
    date.strftime("%b %d %H:%M(%Z)")
  end
  
end


Readline.completion_proc = Proc.new do |word|
  SvnShell::WORDS.grep(/^#{Regexp.quote(word)}/)
end

if ARGV.size != 1
  puts "#{$0} REPOS_PATH"
  exit(1)
end
Svn::Core::Pool.new do |pool|
  SvnShell.new(pool, ARGV.shift).run
end
