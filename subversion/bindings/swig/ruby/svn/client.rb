require "English"
require "svn/error"
require "svn/util"
require "svn/core"
require "svn/wc"
require "svn/ext/client"

module Svn
  module Client
    Util.set_constants(Ext::Client, self)
    Util.set_methods(Ext::Client, self)

    class CommitItem
      class << self
        undef new
      end
    end

    class CommitInfo
      class << self
        undef new
      end

      attr_accessor :pool
      
      alias _date date
      def date
        Util.string_to_time(_date, @pool)
      end
    end

    
    Context = Ctx
    class Context
      class << self
        undef new
        def new(pool)
          obj = Client.create_context(pool)
          obj.__send__("initialize", pool)
          obj
        end
      end
      
      alias _initialize initialize
      def initialize(pool)
        @pool = pool
        @prompts = []
        @providers = []
        update_auth_baton
      end
      undef _initialize

      def checkout(url, path, revision="HEAD", recurse=true)
        Client.checkout(url, path, revision, recurse, self, @pool)
      end
      
      def checkout2(url, path, peg_revision=nil, revision="HEAD", recurse=true)
        Client.checkout2(url, path, peg_revision, revision, recurse, self, @pool)
      end

      def mkdir(paths)
        paths = [paths] unless paths.is_a?(Array)
        Client.mkdir(normalize_path(paths), self, @pool)
      end

      def commit(targets, recurse=true)
        targets = [targets] unless targets.is_a?(Array)
        Util.set_pool(@pool) do
          Client.commit(targets, !recurse, self, @pool)
        end
      end

      def add(path, recurse=true)
        Client.add(path, recurse, self, @pool)
      end

      def delete(paths, force=false)
        paths = [paths] unless paths.is_a?(Array)
        Client.delete(paths, force, self, @pool)
      end
      alias remove delete
      alias rm remove

      def rm_f(paths)
        rm(paths, true)
      end

      def update(paths, rev="HEAD", recurse=true, ignore_externals=false)
        if paths.is_a?(Array)
          Client.update2(paths, rev, recurse, ignore_externals, self, @pool)
        else
          Client.update(paths, rev, recurse, self, @pool)
        end
      end

      def cleanup(dir)
        Client.cleanup(dir, self, @pool)
      end

      def revert(paths, recurse=true)
        paths = [paths] unless paths.is_a?(Array)
        Client.revert(paths, recurse, self, @pool)
      end
      
      def propset(name, value, target, recurse=true, force=false)
        Client.propset2(name, value, target, recurse, force, self, @pool)
      end
      
      def propdel(name, target, recurse=true, force=false)
        Client.propset2(name, nil, target, recurse, force, self, @pool)
      end
      
      def copy(src_path, dst_path, rev=nil)
        Client.copy(src_path, rev || "HEAD", dst_path, self, @pool)
      end
      alias cp copy
      
      def move(src_path, dst_path, rev=nil, force=false)
        Client.move(src_path, rev || "HEAD", dst_path, force, self, @pool)
      end
      alias mv move

      def diff(options, path1, rev1, path2, rev2,
               out_file, err_file, recurse=true,
               ignore_ancestry=false,
               no_diff_deleted=false, force=false)
        Client.diff2(options, path1, rev1, path2, rev2,
                     recurse, ignore_ancestry,
                     no_diff_deleted, force, out_file,
                     err_file, self, @pool)
      end

      def cat(path, rev="HEAD", output=nil)
        used_string_io = output.nil?
        output ||= StringIO.new
        Client.cat(output, path, rev, self, @pool)
        if used_string_io
          output.rewind
          output.read
        else
          output
        end
      end
      
      def cat2(path, peg_rev=nil, rev="HEAD", output=nil)
        used_string_io = output.nil?
        output ||= StringIO.new
        Client.cat2(output, path, peg_rev, rev, self, @pool)
        if used_string_io
          output.rewind
          output.read
        else
          output
        end
      end
      
      def log(paths, start_rev, end_rev, limit,
              discover_changed_paths, strict_node_history)
        paths = [paths] unless paths.is_a?(Array)
        receiver = Proc.new do |changed_paths, rev, author, date, message, pool|
          date = Util.string_to_time(date, pool) if date
          yield(changed_paths, rev, author, date, message, pool)
        end
        Client.log2(paths, start_rev, end_rev, limit,
                    discover_changed_paths,
                    strict_node_history,
                    receiver, self, @pool)
      end
      
      def log_message(paths, start_rev=nil, end_rev=nil)
        start_rev ||= "HEAD"
        end_rev ||= start_rev
        messages = []
        receiver = Proc.new do |changed_paths, rev, author, date, message|
          messages << message
        end
        log(paths, start_rev, end_rev, 0, false, false) do |*args|
          receiver.call(*args)
        end
        if !paths.is_a?(Array) and messages.size == 1
          messages.first
        else
          messages
        end
      end
      
      def add_simple_prompt_provider(retry_limit, prompt=Proc.new)
        args = [retry_limit, @pool]
        klass = Core::AuthCredSimple
        add_prompt_provider("simple", args, prompt, klass)
      end
      
      def add_username_prompt_provider(retry_limit, prompt=Proc.new)
        args = [retry_limit, @pool]
        klass = Core::AuthCredUsername
        add_prompt_provider("username", args, prompt, klass)
      end
      
      def add_ssl_server_trust_prompt_provider(prompt=Proc.new)
        args = [@pool]
        klass = Core::AuthCredSSLServerTrust
        add_prompt_provider("ssl_server_trust", args, prompt, klass)
      end
      
      def add_ssl_client_cert_prompt_provider(retry_limit, prompt=Proc.new)
        args = [retry_limit, @pool]
        klass = Core::AuthCredSSLClientCert
        add_prompt_provider("ssl_client_cert", args, prompt, klass)
      end
      
      def add_ssl_client_cert_pw_prompt_provider(retry_limit, prompt=Proc.new)
        args = [retry_limit, @pool]
        klass = Core::AuthCredSSLClientCertPw
        add_prompt_provider("ssl_client_cert_pw", args, prompt, klass)
      end

      private
      def add_prompt_provider(name, args, prompt, cred_class)
        real_prompt = Proc.new do |*prompt_args|
          cred = cred_class.new
          prompt.call(cred, *prompt_args)
          cred
        end
        pro = Client.__send__("get_#{name}_prompt_provider", real_prompt, *args)
        @prompts << real_prompt
        @providers << pro
        update_auth_baton
      end

      def update_auth_baton
        self.auth_baton = Core::AuthBaton.open(@providers, @pool)
      end

      def normalize_path(paths)
        paths = [paths] unless paths.is_a?(Array)
        paths.collect do |path|
          path.chomp(File::SEPARATOR)
        end
      end
    end
  end
end
