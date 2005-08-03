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

      alias _date date
      def date
        Util.string_to_time(_date)
      end
    end

    
    Context = Ctx
    class Context
      class << self
        undef new
        def new
          obj = Client.create_context
          obj.__send__("initialize")
          obj
        end
      end

      alias _auth_baton auth_baton
      attr_reader :auth_baton
      
      alias _initialize initialize
      def initialize
        @prompts = []
        @providers = []
        @auth_baton = Svn::Core::AuthBaton.new
        self.auth_baton = @auth_baton
        init_callbacks
      end
      undef _initialize

      def checkout(url, path, revision="HEAD", recurse=true)
        Client.checkout(url, path, revision, recurse, self)
      end
      
      def checkout2(url, path, peg_revision=nil, revision="HEAD", recurse=true)
        Client.checkout2(url, path, peg_revision, revision, recurse, self)
      end

      def mkdir(*paths)
        paths = paths.first if paths.size == 1 and paths.first.is_a?(Array)
        Client.mkdir(normalize_path(paths), self)
      end

      def commit(targets, recurse=true)
        targets = [targets] unless targets.is_a?(Array)
        Client.commit(targets, !recurse, self)
      end

      def add(path, recurse=true, force=false, no_ignore=false)
        Client.add3(path, recurse, force, no_ignore, self)
      end

      def delete(paths, force=false)
        paths = [paths] unless paths.is_a?(Array)
        Client.delete(paths, force, self)
      end
      alias remove delete
      alias rm remove

      def rm_f(*paths)
        paths = paths.first if paths.size == 1 and paths.first.is_a?(Array)
        rm(paths, true)
      end

      def update(paths, rev="HEAD", recurse=true, ignore_externals=false)
        if paths.is_a?(Array)
          Client.update2(paths, rev, recurse, ignore_externals, self)
        else
          Client.update(paths, rev, recurse, self)
        end
      end

      def cleanup(dir)
        Client.cleanup(dir, self)
      end

      def revert(paths, recurse=true)
        paths = [paths] unless paths.is_a?(Array)
        Client.revert(paths, recurse, self)
      end
      
      def propset(name, value, target, recurse=true, force=false)
        Client.propset2(name, value, target, recurse, force, self)
      end
      
      def propdel(name, target, recurse=true, force=false)
        Client.propset2(name, nil, target, recurse, force, self)
      end
      
      def copy(src_path, dst_path, rev=nil)
        Client.copy(src_path, rev || "HEAD", dst_path, self)
      end
      alias cp copy
      
      def move(src_path, dst_path, rev=nil, force=false)
        Client.move(src_path, rev || "HEAD", dst_path, force, self)
      end
      alias mv move

      def diff(options, path1, rev1, path2, rev2,
               out_file, err_file, recurse=true,
               ignore_ancestry=false,
               no_diff_deleted=false, force=false)
        Client.diff2(options, path1, rev1, path2, rev2,
                     recurse, ignore_ancestry,
                     no_diff_deleted, force, out_file,
                     err_file, self)
      end

      def cat(path, rev="HEAD", output=nil)
        used_string_io = output.nil?
        output ||= StringIO.new
        Client.cat(output, path, rev, self)
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
        Client.cat2(output, path, peg_rev, rev, self)
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
        receiver = Proc.new do |changed_paths, rev, author, date, message|
          date = Util.string_to_time(date) if date
          yield(changed_paths, rev, author, date, message)
        end
        Client.log2(paths, start_rev, end_rev, limit,
                    discover_changed_paths,
                    strict_node_history,
                    receiver, self)
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

      def revprop(name, uri, rev)
        value, = revprop_get(name, uri, rev)
        value
      end
      
      def revprop_get(name, uri, rev)
        result = Client.revprop_get(name, uri, rev, self)
        if result.is_a?(Array)
          result
        else
          [nil, result]
        end
      end
      
      def revprop_set(name, value, uri, rev, force=false)
        Client.revprop_set(name, value, uri, rev, force, self)
      end
      
      def revprop_del(name, uri, rev, force=false)
        Client.revprop_set(name, nil, uri, rev, force, self)
      end
      
      def switch(path, uri, rev=nil, recurse=true)
        Client.switch(path, uri, rev, recurse, self)
      end
      
      def add_simple_provider
        add_provider(Client.get_simple_provider)
      end

      if Client.respond_to?(:get_windows_simple_provider)
        def add_windows_simple_provider
          add_provider(Client.get_windows_simple_provider)
        end
      end
      
      def add_username_provider
        add_provider(Client.get_username_provider)
      end
      
      def add_simple_prompt_provider(retry_limit, prompt=Proc.new)
        args = [retry_limit]
        klass = Core::AuthCredSimple
        add_prompt_provider("simple", args, prompt, klass)
      end
      
      def add_username_prompt_provider(retry_limit, prompt=Proc.new)
        args = [retry_limit]
        klass = Core::AuthCredUsername
        add_prompt_provider("username", args, prompt, klass)
      end
      
      def add_ssl_server_trust_prompt_provider(prompt=Proc.new)
        args = []
        klass = Core::AuthCredSSLServerTrust
        add_prompt_provider("ssl_server_trust", args, prompt, klass)
      end
      
      def add_ssl_client_cert_prompt_provider(retry_limit, prompt=Proc.new)
        args = [retry_limit]
        klass = Core::AuthCredSSLClientCert
        add_prompt_provider("ssl_client_cert", args, prompt, klass)
      end
      
      def add_ssl_client_cert_pw_prompt_provider(retry_limit, prompt=Proc.new)
        args = [retry_limit]
        klass = Core::AuthCredSSLClientCertPw
        add_prompt_provider("ssl_client_cert_pw", args, prompt, klass)
      end

      def set_log_msg_func(callback=Proc.new)
        self.log_msg_func = nil
        @log_msg_baton = callback
        self.log_msg_baton = callback
      end
      
      def set_notify_func(callback=Proc.new)
        self.notify_func = nil
        @notify_baton = callback
        self.notify_baton = callback
      end
      
      def set_notify_func2(callback=Proc.new)
        self.notify_func2 = nil
        @notify_baton2 = callback
        self.notify_baton2 = callback
      end
      
      def set_cancel_func(callback=Proc.new)
        self.cancel_func = nil
        @cancel_baton = callback
        self.cancel_baton = callback
      end
      
      private
      def init_callbacks
        set_log_msg_func(nil)
        set_notify_func(nil)
        set_notify_func2(nil)
        set_cancel_func(nil)
      end
      %w(log_msg notify cancel).each do |type|
        private "#{type}_func", "#{type}_baton"
        private "#{type}_func=", "#{type}_baton="
      end
      %w(notify).each do |type|
        private "#{type}_func2", "#{type}_baton2"
        private "#{type}_func2=", "#{type}_baton2="
      end
      
      def add_prompt_provider(name, args, prompt, cred_class)
        real_prompt = Proc.new do |*prompt_args|
          cred = cred_class.new
          prompt.call(cred, *prompt_args)
          cred
        end
        pro = Client.__send__("get_#{name}_prompt_provider", real_prompt, *args)
        @prompts << real_prompt
        add_provider(pro)
      end

      def add_provider(provider)
        @providers << provider
        update_auth_baton
      end

      def update_auth_baton
        @auth_baton = Core::AuthBaton.new(@providers, @auth_baton.parameters)
        self.auth_baton = @auth_baton
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
