require "fileutils"

require "svn/client"
require "svn/repos"

module SvnTestUtil

  def setup_basic
    @author = ENV["USER"] || "sample-user"
    @password = "sample-password"
    @realm = "sample realm"
    @repos_path = File.join("test", "repos")
    @full_repos_path = File.expand_path(@repos_path)
    @repos_uri = "file://#{@full_repos_path}"
    @svnserve_host = "127.0.0.1"
    @svnserve_ports = (64152..64282).collect{|x| x.to_s}
    @wc_base_dir = File.join("test", "wc-tmp")
    @wc_path = File.join(@wc_base_dir, "wc")
    @full_wc_path = File.expand_path(@wc_path)
    @tmp_path = File.join("test", "tmp")
    @config_path = File.join("test", "config")
    setup_tmp
    setup_repository
    add_hooks
    setup_svnserve
    setup_config
    setup_wc
    add_authentication
  end

  def teardown_basic
    teardown_svnserve
    teardown_repository
    teardown_wc
    teardown_config
    teardown_tmp
    gc
  end

  def gc
    if $DEBUG
      before_pools = Svn::Core::Pool.number_of_pools
      puts
      puts "before pools: #{before_pools}"
    end
    GC.start
    if $DEBUG
      after_pools = Svn::Core::Pool.number_of_pools
      puts "after pools: #{after_pools}"
      STDOUT.flush
    end
  end

  def change_gc_status(prev_disabled)
    begin
      yield
    ensure
      if prev_disabled
        GC.disable
      else
        GC.enable
      end
    end
  end
  
  def gc_disable(&block)
    change_gc_status(GC.disable, &block)
  end

  def gc_enable(&block)
    change_gc_status(GC.enable, &block)
  end

  def setup_tmp(path=@tmp_path)
    FileUtils.rm_rf(path)
    FileUtils.mkdir_p(path)
  end

  def teardown_tmp(path=@tmp_path)
    FileUtils.rm_rf(path)
  end
  
  def setup_repository(path=@repos_path, config={}, fs_config={})
    FileUtils.rm_rf(path)
    FileUtils.mkdir_p(File.dirname(path))
    Svn::Repos.create(path, config, fs_config)
    @repos = Svn::Repos.open(@repos_path)
    @fs = @repos.fs
  end

  def teardown_repository(path=@repos_path)
    Svn::Repos.delete(path)
    @repos = nil
    @fs = nil
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

  def setup_wc
    teardown_wc
    make_context("").checkout(@repos_uri, @wc_path)
  end

  def teardown_wc
    FileUtils.rm_rf(@wc_base_dir)
  end
  
  def setup_config
    teardown_config
    Svn::Core::Config.ensure(@config_path)
  end

  def teardown_config
    FileUtils.rm_rf(@config_path)
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

  def add_hooks
    add_pre_revprop_change_hook
  end

  def add_pre_revprop_change_hook
    File.open(@repos.pre_revprop_change_hook, "w") do |hook|
      hook.print <<-HOOK
#!/bin/sh
REPOS="$1"
REV="$2"
USER="$3"
PROPNAME="$4"

if [ "$PROPNAME" = "#{Svn::Core::PROP_REVISION_LOG}" -a \
     "$USER" = "#{@author}" ]; then
  exit 0
fi

exit 1
      HOOK
    end
    FileUtils.chmod(0755, @repos.pre_revprop_change_hook)
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
    ctx = Svn::Client::Context.new
    ctx.set_log_msg_func do |items|
      [true, log]
    end
    ctx.add_username_prompt_provider(0) do |cred, realm, username, may_save|
      cred.username = @author
      cred.may_save = false
    end
    setup_auth_baton(ctx.auth_baton)
    ctx
  end

  def setup_auth_baton(auth_baton)
    auth_baton[Svn::Core::AUTH_PARAM_CONFIG_DIR] = @config_path
    auth_baton[Svn::Core::AUTH_PARAM_DEFAULT_USERNAME] = @author
  end
end
