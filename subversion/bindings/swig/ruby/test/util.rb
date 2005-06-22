require "fileutils"

require "svn/client"
require "svn/repos"

module SvnTestUtil

  def setup_basic
    @author = ENV["USER"] || "sample-user"
    @password = "sample-password"
    @realm = "sample realm"
    @pool = Svn::Core::Pool.new(nil)
    @repos_path = File.join("test", "repos")
    @full_repos_path = File.expand_path(@repos_path)
    @repos_uri = "file://#{@full_repos_path}"
    @svnserve_host = "127.0.0.1"
    @svnserve_ports = (64152..64282).collect{|x| x.to_s}
    @wc_path = File.join("test", "wc")
    setup_repository(@repos_path)
    @repos = Svn::Repos.open(@repos_path, @pool)
    @fs = @repos.fs
    FileUtils.rm_rf(@wc_path)
    make_context("").checkout(@repos_uri, @wc_path)
    add_authentication
    setup_svnserve
  end

  def teardown_basic
    @pool.destroy
    teardown_svnserve
    teardown_repository(@repos_path)
    FileUtils.rm_rf(@wc_path)
  end

  def setup_repository(path, config={}, fs_config={})
    FileUtils.rm_rf(path)
    FileUtils.mkdir_p(File.dirname(path))
    Svn::Core::Pool.new do |pool|
      Svn::Repos.create(path, config, fs_config, pool)
    end
  end

  def teardown_repository(path)
    Svn::Core::Pool.new do |pool|
      Svn::Repos.delete(path, pool)
    end
  end

  def setup_svnserve
    @svnserve_port = nil
    @repos_svnserve_uri = nil
    @svnserve_ports.each do |port|
      @svnserve_pid = fork {
        STDERR.close
        exec("svnserve",
             "--listen-host", @svnserve_host,
             "--listen-port", port,
             "-d", "--foreground")
      }
      pid, status = Process.waitpid2(@svnserve_pid, Process::WNOHANG)
      if status and status.exited?
        STDERR.puts "port #{port} couldn't be used for svnserve"
      else
        @svnserve_port = port
        @repos_svnserve_uri =
          "svn://#{@svnserve_host}:#{@svnserve_port}#{@full_repos_path}"
        break
      end
    end
    if @svnserve_port.nil?
      msg = "Can't run svnserve because available port "
      msg << "isn't exist in [#{@svnserve_ports.join(', ')}]"
      raise msg
    end
  end

  def teardown_svnserve
    if @svnserve_pid
      Process.kill(:TERM, @svnserve_pid)
      begin
        Process.waitpid(@svnserve_pid)
      rescue Errno::ECHILD
      end
    end
  end
  
  def add_authentication
    passwd_file = "passwd"
    File.open(@repos.svnserve_conf, "w") do |conf|
      conf.print <<-CONF
[general]
anon-access = none
auth-access = write
password-db = #{passwd_file}
realm = #{@realm}
      CONF
    end
    File.open(File.join(@repos.conf_dir, passwd_file), "w") do |f|
      f.print <<-PASSWD
[users]
#{@author} = #{@password}
      PASSWD
    end
  end
  
  def youngest_rev
    @fs.youngest_rev
  end

  def root(rev=nil)
    @fs.root(rev)
  end

  def prop(name, rev=nil)
    @fs.prop(name, rev)
  end

  def make_context(log)
    ctx = Svn::Client::Context.new(@pool)
    ctx.log_msg_func = Proc.new do |items|
      [true, log]
    end
    ctx.add_username_prompt_provider(0) do |cred, realm, may_save, pool|
      cred.username = @author
      cred.may_save = false
    end
    ctx
  end
  
end
