require "English"
require "tempfile"
require "svn/error"
require "svn/util"
require "svn/delta"
require "svn/ext/fs"

module Svn
  module Fs
    Util.set_constants(Ext::Fs, self)
    Util.set_methods(Ext::Fs, self)

    class << self
      alias dir? is_dir
    end

    FileSystem = SWIG::TYPE_p_svn_fs_t
    class FileSystem

      class << self
        def new(config, pool)
          Util.set_pool(pool) do
            Fs.new(config, pool)
          end
        end

        def create(path, config, pool)
          Util.set_pool(pool) do
            Fs.create(path, config, pool)
          end
        end

        def open(path, config, pool)
          Util.set_pool(pool) do
            Fs.open(path, config, pool)
          end
        end
      end
      
      attr_accessor :pool
      
      def open_txn(name)
        Util.set_pool(@pool) do
          Fs.open_txn(self, name, @pool)
        end
      end

      def transaction(rev=nil)
        txn = nil
        Util.set_pool(@pool) do
          txn = Fs.begin_txn(self, rev || youngest_rev, @pool)
        end
        
        if block_given?
          yield(txn)
          txn.commit if transactions.include?(txn.name)
        else
          txn
        end
      end
      
      def youngest_rev
        Fs.youngest_rev(self, @pool)
      end

      def prop(name, rev=nil)
        Fs.revision_prop(self, rev || youngest_rev, name, @pool)
      end

      def transactions
        Fs.list_transactions(self, @pool)
      end

      def root(rev=nil)
        Util.set_pool(@pool) do
          Fs.revision_root(self, rev || youngest_rev, @pool)
        end
      end
    end

    
    Transaction = SWIG::TYPE_p_svn_fs_txn_t
    class Transaction

      attr_accessor :pool

      def name
        Fs.txn_name(self, @pool)
      end
      
      def prop(name)
        Fs.txn_prop(self, name, @pool)
      end

      def base_revision
        Fs.txn_base_revision(self)
      end

      def root
        Util.set_pool(@pool) do
          Fs.txn_root(self, @pool)
        end
      end

      def proplist
        Fs.txn_proplist(self, @pool)
      end

      def abort
        Fs.abort_txn(self, @pool)
      end

      def commit
        result = Fs.commit(self, @pool)
        if result.is_a?(Array)
          result
        else
          [nil, result]
        end
      end
    end


    Root = SWIG::TYPE_p_svn_fs_root_t
    class Root
      attr_accessor :pool
      attr_reader :editor

      def revision
        Fs.revision_root_revision(self)
      end

      def fs
        Util.set_pool(@pool) do
          Fs.root_fs(self)
        end
      end
      
      def node_id(path)
        Fs.node_id(self, path, @pool)
      end

      def node_created_rev(path)
        Fs.node_created_rev(self, path, @pool)
      end

      def node_prop(path, key)
        Fs.node_prop(self, path, key, @pool)
      end
      
      def node_proplist(path)
        Fs.node_proplist(self, path, @pool)
      end
      alias node_prop_list node_proplist

      def dir?(path)
        Fs.dir?(self, path, @pool)
      end

      def check_path(path)
        Fs.check_path(self, path, @pool)
      end

      def file_length(path)
        Fs.file_length(self, path, @pool)
      end

      def file_contents(path)
        stream = Fs.file_contents(self, path, @pool)
        Util.set_pool(@pool){stream}
        if block_given?
          begin
            yield stream
          ensure
            stream.close
          end
        else
          stream
        end
      end

      def close
        Fs.close_root(self)
      end

      def dir_entries(path)
        tmp = {}
        Fs.dir_entries(self, path, @pool).each do |name, entry|
          tmp[name] = Util.set_pool(@pool) {entry}
        end
        tmp
      end

      def editor=(editor)
        @editor = editor
        @svn_editor, @baton = Delta.make_editor(@editor, @pool)
      end

      def dir_delta(src_path, src_entry, tgt_root, tgt_path,
                    text_deltas=false, recurse=true,
                    entry_props=false, ignore_ancestry=false,
                    &authz_read_func)
        authz_read_func = Proc.new{true} if authz_read_func.nil?
        Repos.dir_delta(self, src_path, src_entry,
                        tgt_root, tgt_path,
                        @svn_editor, @baton, authz_read_func,
                        text_deltas, recurse, entry_props,
                        ignore_ancestry, @pool)
      end

      def replay(editor, edit_baton, edit_pool)
        Repos.replay(self, editor, edit_baton, edit_pool)
      end

      def copied_from(path)
        Fs.copied_from(self, path, @pool)
      end
    end


    DirectoryEntry = Dirent
    class DirectoryEntry
      attr_accessor :pool

      alias _id id
      def id
        Util.set_pool(@pool) do
          _id
        end
      end
    end
    
    
    Id = SWIG::TYPE_p_svn_fs_id_t
    class Id
      attr_accessor :pool
      
      def to_s
        unparse
      end
      
      def unparse
        Fs.unparse_id(self, pool)
      end
    end

    
    class FileDiff

      def initialize(root1, path1, root2, path2, pool)
        @tempfile1 = nil
        @tempfile2 = nil

        @binary = nil

        @root1 = root1
        @path1 = path1
        @root2 = root2
        @path2 = path2

        @pool = pool
      end

      def binary?
        if @binary.nil?
          @binary = _binary?(@root1, @path1)
          @binary ||= _binary?(@root2, @path2)
        end
        @binary
      end

      def files
        if @tempfile1
          [@tempfile1, @tempfile2]
        else
          @tempfile1 = Tempfile.new("svn_fs")
          @tempfile2 = Tempfile.new("svn_fs")

          dump_contents(@tempfile1, @root1, @path1)
          dump_contents(@tempfile2, @root2, @path2)

          [@tempfile1, @tempfile2]
        end
      end

      def diff
        files
        @diff ||= Util.set_pool(@pool) do
          Core::Diff.file_diff(@tempfile1.path, @tempfile2.path, @pool)
        end
      end

      def unified(label1, label2)
        if diff and diff.diff?
          diff.unified(label1, label2)
        else
          ""
        end
      end
      
      private
      def dump_contents(tempfile, root, path)
        if root and path
          begin
            tempfile.open
            root.file_contents(path) do |stream|
              tempfile.print(stream.read)
            end
          ensure
            tempfile.close
          end
        end
      end

      def _binary?(root, path)
        if root and path
          prop = root.node_prop(path, Core::PROP_MIME_TYPE)
          prop and Core.binary_mime_type?(prop)
        end
      end
    end
  end
end
