require "English"
require 'uri'
require "svn/error"
require "svn/util"
require "svn/core"
require "svn/wc"
require "svn/ra"
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

    class CommitItem2
      class << self
        undef new
      end
    end

    class Info
      alias url URL
      alias repos_root_url repos_root_URL
    end

    PropListItem = ProplistItem
    # Following methods are also available:
    #
    # [name]
    #   Returns an URI for the item concerned with the instance.
    # [props]
    #   Returns a Hash of properties, such as
    #   <tt>{propname1 => propval1, propname2 => propval2, ...}</tt>.
    class PropListItem
      alias name node_name
      alias props prop_hash

      def method_missing(meth, *args)
        _props = props
        if _props.respond_to?(meth)
          _props.__send__(meth, *args)
        else
          super
        end
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
        @batons = []
        @providers = []
        @auth_baton = Svn::Core::AuthBaton.new
        self.auth_baton = @auth_baton
        init_callbacks
      end
      undef _initialize

      def checkout(url, path, revision=nil, peg_rev=nil,
                   recurse=true, ignore_externals=false)
        revision ||= "HEAD"
        Client.checkout2(url, path, peg_rev, revision,
                         recurse, ignore_externals, self)
      end
      alias co checkout

      def mkdir(*paths)
        paths = paths.first if paths.size == 1 and paths.first.is_a?(Array)
        Client.mkdir2(normalize_path(paths), self)
      end

      def commit(targets, recurse=true, keep_locks=false)
        targets = [targets] unless targets.is_a?(Array)
        Client.commit3(targets, recurse, keep_locks, self)
      end
      alias ci commit

      def status(path, rev=nil, recurse=true, get_all=false,
                 update=true, no_ignore=false,
                 ignore_externals=false, &status_func)
        Client.status2(path, rev, status_func,
                       recurse, get_all, update, no_ignore,
                       ignore_externals, self)
      end
      alias st status

      def add(path, recurse=true, force=false, no_ignore=false)
        Client.add3(path, recurse, force, no_ignore, self)
      end

      def delete(paths, force=false)
        paths = [paths] unless paths.is_a?(Array)
        Client.delete2(paths, force, self)
      end
      alias del delete
      alias remove delete
      alias rm remove

      def rm_f(*paths)
        paths = paths.first if paths.size == 1 and paths.first.is_a?(Array)
        rm(paths, true)
      end

      def update(paths, rev="HEAD", recurse=true, ignore_externals=false)
        paths_is_array = paths.is_a?(Array)
        paths = [paths] unless paths_is_array
        result = Client.update2(paths, rev, recurse, ignore_externals, self)
        result = result.first unless paths_is_array
        result
      end
      alias up update

      def import(path, uri, recurse=true, no_ignore=false)
        Client.import2(path, uri, !recurse, no_ignore, self)
      end
      
      def cleanup(dir)
        Client.cleanup(dir, self)
      end

      def relocate(dir, from, to, recurse=true)
        Client.relocate(dir, from, to, recurse, self)
      end

      def revert(paths, recurse=true)
        paths = [paths] unless paths.is_a?(Array)
        Client.revert(paths, recurse, self)
      end

      def resolved(path, recurse=true)
        Client.resolved(path, recurse, self)
      end
      
      def propset(name, value, target, recurse=true, force=false)
        Client.propset2(name, value, target, recurse, force, self)
      end
      alias prop_set propset
      alias pset propset
      alias ps propset
      
      def propdel(name, target, recurse=true, force=false)
        Client.propset2(name, nil, target, recurse, force, self)
      end
      alias prop_del propdel
      alias pdel propdel
      alias pd propdel

      # Returns a value of a property, with +name+ attached to +target+,
      # as a Hash such as <tt>{uri1 => value1, uri2 => value2, ...}</tt>.
      def propget(name, target, rev=nil, peg_rev=nil, recurse=true)
        rev ||= "HEAD"
        peg_rev ||= rev
        Client.propget2(name, target, rev, peg_rev, recurse, self)
      end
      alias prop_get propget
      alias pget propget
      alias pg propget

      # Returns list of properties attached to +target+ as an Array of
      # Svn::Client::PropListItem.
      # Paths and URIs are available as +target+.
      def proplist(target, rev=nil, peg_rev=nil, recurse=true)
        rev ||= "HEAD"
        peg_rev ||= rev
        Client.proplist2(target, rev, peg_rev, recurse, self)
      end
      alias prop_list proplist
      alias plist proplist
      alias pl proplist
      
      def copy(src_path, dst_path, rev=nil)
        Client.copy3(src_path, rev || "HEAD", dst_path, self)
      end
      alias cp copy
      
      def move(src_path, dst_path, force=false)
        Client.move4(src_path, dst_path, force, self)
      end
      alias mv move

      def mv_f(src_path, dst_path)
        move(src_path, dst_path, true)
      end

      def diff(options, path1, rev1, path2, rev2,
               out_file, err_file, recurse=true,
               ignore_ancestry=false,
               no_diff_deleted=false, force=false,
               header_encoding=nil)
        header_encoding ||= Core::LOCALE_CHARSET
        Client.diff3(options, path1, rev1, path2, rev2,
                     recurse, ignore_ancestry,
                     no_diff_deleted, force, header_encoding,
                     out_file, err_file, self)
      end

      def diff_peg(options, path, start_rev, end_rev,
                   out_file, err_file, peg_rev=nil,
                   recurse=true, ignore_ancestry=false,
                   no_diff_deleted=false, force=false,
                   header_encoding=nil)
        header_encoding ||= Core::LOCALE_CHARSET
        Client.diff_peg3(options, path, peg_rev, start_rev, end_rev,
                         recurse, ignore_ancestry,
                         no_diff_deleted, force, header_encoding,
                         out_file, err_file, self)
      end

      # Invokes block once for each item changed between <tt>path1</tt>
      # at <tt>rev1</tt> and <tt>path2</tt> at <tt>rev2</tt>,
      # and returns +nil+.
      # +diff+ is an instance of Svn::Client::DiffSummarize.
      def diff_summarize(path1, rev1, path2, rev2,
                         recurse=true, ignore_ancestry=true,
                         &block) # :yields: diff
        Client.diff_summarize(path1, rev1, path2, rev2,
                              recurse, ignore_ancestry, block, self)
      end

      def diff_summarize_peg(path1, rev1, rev2, peg_rev=nil,
                             recurse=true, ignore_ancestry=true,
                             &block)
        Client.diff_summarize_peg(path1, rev1, rev2, peg_rev,
                                  recurse, ignore_ancestry, block, self)
      end

      def merge(src1, rev1, src2, rev2, target_wcpath,
                recurse=true, ignore_ancestry=false,
                force=false, dry_run=false, options=nil)
        Client.merge2(src1, rev1, src2, rev2, target_wcpath,
                      recurse, ignore_ancestry, force,
                      dry_run, options, self)
      end

      def merge_peg(src, rev1, rev2, target_wcpath,
                    peg_rev=nil, recurse=true,
                    ignore_ancestry=false, force=false,
                    dry_run=false, options=nil)
        Client.merge_peg2(src, rev1, rev2, peg_rev,
                          target_wcpath, recurse, ignore_ancestry,
                          force, dry_run, options, self)
      end
      
      # Returns a content of +path+ at +rev+ as a String.
      def cat(path, rev="HEAD", peg_rev=nil, output=nil)
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

      def lock(targets, comment=nil, steal_lock=false)
        targets = [targets] unless targets.is_a?(Array)
        Client.lock(targets, comment, steal_lock, self)
      end
      
      def unlock(targets, break_lock=false)
        targets = [targets] unless targets.is_a?(Array)
        Client.unlock(targets, break_lock, self)
      end

      def info(path_or_uri, rev=nil, peg_rev=nil, recurse=false)
        rev ||= URI(path_or_uri).scheme ? "HEAD" : "BASE"
        peg_rev ||= rev
        receiver = Proc.new do |path, info|
          yield(path, info)
        end
        Client.info(path_or_uri, rev, peg_rev, receiver, recurse, self)
      end

      # Returns URL for +path+ as a String.
      def url_from_path(path)
        Client.url_from_path(path)
      end
      
      def uuid_from_path(path, adm)
        Client.uuid_from_path(path, adm, self)
      end
      
      # Returns UUID for +url+ as a String.
      def uuid_from_url(url)
        Client.uuid_from_url(url, self)
      end

      def open_ra_session(url)
        Client.open_ra_session(url, self)
      end
      
      # Scans revisions from +start_rev+ to +end_rev+ for each path in
      # +paths+, invokes block once for each revision, and then returns
      # +nil+.
      #
      # When +discover_changed_paths+ is +false+ or +nil+, +changed_paths+,
      # the first block-argument, is +nil+.  Otherwise, it is a Hash
      # containing simple information associated with the revision,
      # whose keys are paths and values are changes, such as
      # <tt>{path1 => change1, path2 => change2, ...}</tt>,
      # where each path is an absolute one in the repository and each
      # change is a instance of Svn::Core::LogChangedPath.
      # The rest of the block arguments, +rev+, +author+, +date+, and
      # +message+ are the revision number, author, date, and the log
      # message of that revision, respectively.
      def log(paths, start_rev, end_rev, limit,
              discover_changed_paths, strict_node_history,
              peg_rev=nil)
        paths = [paths] unless paths.is_a?(Array)
        receiver = Proc.new do |changed_paths, rev, author, date, message|
          date = Time.from_svn_format(date) if date
          yield(changed_paths, rev, author, date, message)
        end
        Client.log3(paths, peg_rev, start_rev, end_rev, limit,
                    discover_changed_paths,
                    strict_node_history,
                    receiver, self)
      end

      # Returns log messages, for commits affecting +paths+ from +start_rev+
      # to +end_rev+, as an Array of String.
      # You can use URIs as well as paths as +paths+.
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

      def blame(path_or_uri, start_rev=nil, end_rev=nil, peg_rev=nil,
                diff_options=nil, ignore_mime_type=false)
        start_rev ||= 1
        end_rev ||= URI(path_or_uri).scheme ? "HEAD" : "BASE"
        peg_rev ||= end_rev
        diff_options ||= Svn::Core::DiffFileOptions.new
        receiver = Proc.new do |line_no, revision, author, date, line|
          date = Time.from_svn_format(date) if date
          yield(line_no, revision, author, date, line)
        end
        Client.blame3(path_or_uri, peg_rev, start_rev,
                      end_rev, diff_options, ignore_mime_type,
                      receiver, self)
      end
      alias praise blame
      alias annotate blame
      alias ann annotate
      
      # Returns a value of a revision property named +name+ for +uri+
      # at +rev+, as a String.
      # Both URLs and paths are available as +uri+.
      def revprop(name, uri, rev)
        value, = revprop_get(name, uri, rev)
        value
      end
      alias rp revprop
      
      # Returns a value of a revision property named +name+ for +uri+
      # at +rev+, as an Array such as <tt>[value, rev]</tt>.
      # Both URLs and paths are available as +uri+.
      def revprop_get(name, uri, rev)
        result = Client.revprop_get(name, uri, rev, self)
        if result.is_a?(Array)
          result
        else
          [nil, result]
        end
      end
      alias rpget revprop_get
      alias rpg revprop_get
      
      # Sets +value+ as a revision property named +name+ for +uri+ at +rev+.
      # Both URLs and paths are available as +uri+.
      def revprop_set(name, value, uri, rev, force=false)
        Client.revprop_set(name, value, uri, rev, force, self)
      end
      alias rpset revprop_set
      alias rps revprop_set
      
      # Deletes a revision property, named +name+, for +uri+ at +rev+.
      # Both URLs and paths are available as +uri+.
      def revprop_del(name, uri, rev, force=false)
        Client.revprop_set(name, nil, uri, rev, force, self)
      end
      alias rpdel revprop_del
      alias rpd revprop_del

      # Returns a list of revision properties set for +uri+ at +rev+,
      # as an Array such as
      # <tt>[{revprop1 => value1, revprop2 => value2, ...}, rev]</tt>.
      # Both URLs and paths are available as +uri+.
      def revprop_list(uri, rev)
        props, rev = Client.revprop_list(uri, rev, self)
        if props.has_key?(Svn::Core::PROP_REVISION_DATE)
          props[Svn::Core::PROP_REVISION_DATE] =
            Time.from_svn_format(props[Svn::Core::PROP_REVISION_DATE])
        end
        [props, rev]
      end
      alias rplist revprop_list
      alias rpl revprop_list

      def export(from, to, rev=nil, peg_rev=nil,
                 force=false, ignore_externals=false,
                 recurse=true, native_eol=nil)
        Client.export3(from, to, rev, peg_rev, force,
                       ignore_externals, recurse, native_eol, self)
      end
      
      def ls(path_or_uri, rev=nil, peg_rev=nil, recurse=false)
        rev ||= URI(path_or_uri).scheme ? "HEAD" : "BASE"
        peg_rev ||= rev
        Client.ls3(path_or_uri, rev, peg_rev, recurse, self)
      end

      # Invokes block once for each path below +path_or_uri+ at +rev+
      # and returns +nil+.
      # +path+ is a relative path from the +path_or_uri+.
      # +dirent+ is an instance of Svn::Core::Dirent.
      # +abs_path+ is an absolute path for +path_or_uri+ in the repository.
      def list(path_or_uri, rev, peg_rev=nil, recurse=false,
               dirent_fields=nil, fetch_locks=true,
               &block) # :yields: path, dirent, lock, abs_path
        dirent_fields ||= Core::DIRENT_ALL
        Client.list(path_or_uri, peg_rev, rev, recurse, dirent_fields,
                    fetch_locks, block, self)
      end

      def switch(path, uri, rev=nil, recurse=true)
        Client.switch(path, uri, rev, recurse, self)
      end
      
      def add_simple_provider
        add_provider(Core.auth_get_simple_provider)
      end

      if Core.respond_to?(:get_windows_simple_provider)
        def add_windows_simple_provider
          add_provider(Core.auth_get_windows_simple_provider)
        end
      end
      
      if Core.respond_to?(:get_keychain_simple_provider)
        def add_keychain_simple_provider
          add_provider(Core.auth_get_keychain_simple_provider)
        end
      end
      
      def add_username_provider
        add_provider(Core.auth_get_username_provider)
      end

      def add_ssl_client_cert_file_provider
        add_provider(Core.auth_get_ssl_client_cert_file_provider)
      end

      def add_ssl_client_cert_pw_file_provider
        add_provider(Core.auth_get_ssl_client_cert_pw_file_provider)
      end

      def add_ssl_server_trust_file_provider
        add_provider(Core.auth_get_ssl_server_trust_file_provider)
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
        @log_msg_baton = Client.set_log_msg_func2(self, callback)
      end
      
      def set_notify_func(callback=Proc.new)
        @notify_baton = Client.set_notify_func2(self, callback)
      end
      
      def set_cancel_func(callback=Proc.new)
        @cancel_baton = Client.set_cancel_func(self, callback)
      end
      
      private
      def init_callbacks
        set_log_msg_func(nil)
        set_notify_func(nil)
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
        method_name = "swig_rb_auth_get_#{name}_prompt_provider"
        baton, pro = Core.__send__(method_name, real_prompt, *args)
        @batons << baton
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

    # Following methods are also available:
    #
    # [path]
    #   Returns a path concerned with the instance.
    # [prop_changed?]
    #   Returns +true+ when the instance is a change involving a property
    #   change.
    class DiffSummarize
      alias prop_changed? prop_changed

      # Returns +true+ when the instance is a normal change.
      def kind_normal?
        summarize_kind == DIFF_SUMMARIZE_KIND_NORMAL
      end

      # Returns +true+ when the instance is a change involving addition.
      def kind_added?
        summarize_kind == DIFF_SUMMARIZE_KIND_ADDED
      end

      # Returns +true+ when the instance is a change involving modification.
      def kind_modified?
        summarize_kind == DIFF_SUMMARIZE_KIND_MODIFIED
      end

      # Returns +true+ when the instance is a change involving deletion.
      def kind_deleted?
        summarize_kind == DIFF_SUMMARIZE_KIND_DELETED
      end

      # Returns +true+ when the instance is a change made to no node.
      def node_kind_none?
        node_kind == Core::NODE_NONE
      end

      # Returns +true+ when the instance is a change made to a file node.
      def node_kind_file?
        node_kind == Core::NODE_FILE
      end

      # Returns +true+ when the instance is a change made to a directory node.
      def node_kind_dir?
        node_kind == Core::NODE_DIR
      end

      # Returns +true+ when the instance is a change made to an unknown node.
      def node_kind_unknown?
        node_kind == Core::NODE_UNKNOWN
      end
    end
  end
end
