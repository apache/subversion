require "English"
require "svn/error"
require "svn/util"
require "svn/core"
require "svn/fs"
require "svn/ext/repos"

module Svn
  module Repos
    Util.set_constants(Ext::Repos, self)
    Util.set_methods(Ext::Repos, self)


    class << self
      alias_method :_create, :create
      alias_method :_open, :open
    end
    alias_method :_create, :create
    alias_method :_open, :open
    
    module_function
    def open(path, pool)
      Util.set_pool(pool) do
        _open(path, pool)
      end
    end

    def create(path, config, fs_config, pool)
      Util.set_pool(pool) do
        _create(path, nil, nil, config, fs_config, pool)
      end
    end

    ReposCore = SWIG::TYPE_p_svn_repos_t
    class ReposCore
      class << self
        def def_simple_delegate(*ids)
          ids.each do |id|
            module_eval(<<-EOC, __FILE__, __LINE__)
            def #{id.to_s}
              Repos.#{id.to_s}(self, @pool)
            end
            EOC
          end
        end
      end
      
      attr_accessor :pool

      def_simple_delegate :path, :db_env, :conf_dir
      def_simple_delegate :svnserve_conf, :lock_dir
      def_simple_delegate :start_commit_hook
      def_simple_delegate :pre_commit_hook, :post_commit_hook
      def_simple_delegate :pre_revprop_change_hook, :post_revprop_change_hook
      
      
      def fs
        @fs ||= Util.set_pool(@pool) do
          Repos.fs(self)
        end
      end

      def youngest_rev
        fs.youngest_rev
      end

      def dated_revision(date)
        Repos.dated_revision(self, Util.to_apr_time(date), @pool)
      end

      def transaction_for_commit(author, log, rev=nil)
        txn = nil
        args = [self, rev || youngest_rev, author, log, @pool]
        Util.set_pool(@pool) do
          txn = Repos.fs_begin_txn_for_commit(*args)
        end
        
        if block_given?
          yield(txn)
          commit(txn) if fs.transactions.include?(txn.name)
        else
          txn
        end
      end

      def commit(txn)
        Repos.fs_commit_txn(self, txn, @pool)
      end

      def node_editor(base_root, root, edit_pool)
        Repos.node_editor(self, base_root, root, @pool, edit_pool)
      end

      def delta_tree(root, base_rev)
        edit_pool = Core::Pool.new(@pool)
        begin
          base_root = fs.root(base_rev)
          editor, edit_baton = node_editor(base_root, root, edit_pool)
          root.replay(editor, edit_baton, edit_pool)
          Repos.node_from_baton(edit_baton)
        ensure
          edit_pool.destroy
        end
      end
    end


    class Node

      alias text_mod? text_mod
      alias prop_mod? prop_mod
      
      def copy?
        Util.copy?(copyfrom_path, copyfrom_rev)
      end

      def add?
        action == "A"
      end

      def delete?
        action == "D"
      end

      def replace?
        action == "R"
      end

      def file?
        kind == Core::NODE_FILE
      end

      def dir?
        kind == Core::NODE_DIR
      end

      def none?
        kind == Core::NODE_NONE
      end

      def unknown?
        kind == Core::NODE_UNKNOWN
      end
      
    end
  end
end
