require "fileutils"

require "svn/core"
require "svn/client"
require "svn/repos"

module SvnTestUtil

  def setup_basic
    @author = ENV["USER"] || "sample-user"
    @pool = Svn::Core::Pool.new(nil)
    @repos_path = File.join("test", "repos")
    @repos_uri = "file://#{File.expand_path(@repos_path)}"
    @wc_path = File.join("test", "wc")
    setup_repository(@repos_path)
    @repos = Svn::Repos.open(@repos_path, @pool)
    @fs = @repos.fs
    make_context("").checkout(@repos_uri, @wc_path)
  end

  def teardown_basic
    @pool.destroy
    teardown_repository(@repos_path)
    FileUtils.rm_rf(@wc_path)
  end

  def setup_repository(path, config={}, fs_config={})
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
