require "English"
require "svn/error"
require "svn/util"
require "svn/core"
require "svn/ext/delta"

module Svn
  module Delta
    Util.set_constants(Ext::Delta, self)
    Util.set_methods(Ext::Delta, self)

    class << self
      alias make_editor swig_rb_make_editor
    end

    remove_const(:Editor)
    class Editor
      # open_root -> add_directory -> open_directory -> add_file -> open_file 
      def set_target_revision(target_revision, pool)
      end
      
      def open_root(base_revision, dir_pool)
      end
        
      def delete_entry(path, revision, parent_baton, pool)
      end

      def add_directory(path, parent_baton,
                        copyfrom_path, copyfrom_revision, dir_pool)
      end

      def open_directory(path, parent_baton, base_revision, dir_pool)
      end

      def change_dir_prop(dir_baton, name, value, pool)
      end

      def close_directory(dir_baton)
      end

      def absent_directory(path, parent_baton, pool)
      end

      def add_file(path, parent_baton,
                   copyfrom_path, copyfrom_revision, file_pool)
      end

      def open_file(path, parent_baton, base_revision, file_pool)
      end

      # return nil or object which has `call' method.
      def apply_textdelta(file_baton, base_checksum, pool)
      end

      def change_file_prop(file_baton, name, value, pool)
      end

      def close_file(file_baton, text_checksum)
      end

      def absent_file(path, parent_baton, pool)
      end

      def close_edit(baton)
      end

      def abort_edit(baton)
      end
    end

    class CopyDetectableEditor < Editor
      def add_directory(path, parent_baton,
                        copyfrom_path, copyfrom_revision, dir_pool)
      end

      def add_file(path, parent_baton,
                   copyfrom_path, copyfrom_revision, file_pool)
      end
    end
    
    class ChangedDirsEditor < Editor
      attr_reader :changed_dirs

      def initialize
        @changed_dirs = []
      end
      
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
      
      def close_edit(baton)
        @changed_dirs.sort!
      end
      
      private
      def dir_changed(baton)
        if baton[0]
          # the directory hasn't been printed yet. do it.
          @changed_dirs << "#{baton[1]}/"
          baton[0] = nil
        end
      end
    end

    class ChangedEditor < Editor

      attr_reader :copied_files, :copied_dirs
      attr_reader :added_files, :added_dirs
      attr_reader :deleted_files, :deleted_dirs
      attr_reader :updated_files, :updated_dirs
      
      def initialize(root, base_root)
        @root = root
        @base_root = base_root
        @in_copied_dir = []
        @copied_files = []
        @copied_dirs = []
        @added_files = []
        @added_dirs = []
        @deleted_files = []
        @deleted_dirs = []
        @updated_files = []
        @updated_dirs = []
      end
      
      def open_root(base_revision, dir_pool)
        [true, '']
      end
      
      def delete_entry(path, revision, parent_baton, pool)
        if @base_root.dir?("/#{path}")
          @deleted_dirs << "#{path}/"
        else
          @deleted_files << path
        end
      end
      
      def add_directory(path, parent_baton,
                        copyfrom_path, copyfrom_revision, dir_pool)
        copyfrom_rev, copyfrom_path = @root.copied_from(path)
        if in_copied_dir?
          @in_copied_dir.push(true)
        elsif Util.copy?(copyfrom_path, copyfrom_rev)
          @copied_dirs << ["#{path}/", "#{copyfrom_path.sub(/^\//, '')}/", copyfrom_rev]
          @in_copied_dir.push(true)
        else
          @added_dirs << "#{path}/"
          @in_copied_dir.push(false)
        end
        [false, path]
      end
      
      def open_directory(path, parent_baton, base_revision, dir_pool)
        [true, path]
      end
      
      def change_dir_prop(dir_baton, name, value, pool)
        if dir_baton[0]
          @updated_dirs << "#{dir_baton[1]}/"
          dir_baton[0] = false
        end
        dir_baton
      end
      
      def close_directory(dir_baton)
        unless dir_baton[0]
          @in_copied_dir.pop
        end
      end

      def add_file(path, parent_baton,
                   copyfrom_path, copyfrom_revision, file_pool)
        copyfrom_rev, copyfrom_path = @root.copied_from(path)
        if in_copied_dir?
          # do nothing
        elsif Util.copy?(copyfrom_path, copyfrom_rev)
          @copied_files << [path, copyfrom_path.sub(/^\//, ''), copyfrom_rev]
        else
          @added_files << path
        end
        [nil, nil, nil]
      end
      
      def open_file(path, parent_baton, base_revision, file_pool)
        [nil, nil, path]
      end
      
      def apply_textdelta(file_baton, base_checksum, pool)
        file_baton[0] = :update
        nil
      end
      
      def change_file_prop(file_baton, name, value, pool)
        file_baton[1] = :update
      end
      
      def close_file(file_baton, text_checksum)
        text_mod, prop_mod, path = file_baton
        # test the path. it will be nil if we added this file.
        if path
          if [text_mod, prop_mod] != [nil, nil]
            @updated_files << path
          end
        end
      end

      def close_edit(baton)
        @copied_files.sort! {|a, b| a[0] <=> b[0]}
        @copied_dirs.sort! {|a, b| a[0] <=> b[0]}
        @added_files.sort!
        @added_dirs.sort!
        @deleted_files.sort!
        @deleted_dirs.sort!
        @updated_files.sort!
        @updated_dirs.sort!
      end

      private
      def in_copied_dir?
        @in_copied_dir.last
      end
    end
    
  end
end
