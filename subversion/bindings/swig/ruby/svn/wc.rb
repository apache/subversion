require "English"
require "svn/error"
require "svn/util"
require "svn/core"
require "svn/delta"
require "svn/ext/wc"

module Svn
  module Wc
    Util.set_constants(Ext::Wc, self)
    Util.set_methods(Ext::Wc, self)
    self.swig_init_asp_dot_net_hack()

    @@alias_targets = %w(parse_externals_description
                         ensure_adm cleanup)
    class << self
      @@alias_targets.each do |target|
        alias_method "_#{target}", target
      end
    end
    @@alias_targets.each do |target|
      alias_method "_#{target}", target
    end
    @@alias_targets = nil
    
    module_function
    def locked?(path)
      Wc.locked(path)
    end

    def ensure_adm(path, uuid, url, repos, revision)
      Wc.ensure_adm2(path, uuid, url, repos, revision)
    end

    def parse_externals_description(parent_dir, desc)
      Wc.parse_externals_description2(parent_dir, desc)
    end

    def actual_target(path)
      Wc.get_actual_target(path)
    end

    def normal_prop?(name)
      Wc.is_normal_prop(name)
    end
    
    def wc_prop?(name)
      Wc.is_wc_prop(name)
    end
    
    def entry_prop?(name)
      Wc.is_entry_prop(name)
    end

    def pristine_copy_path(path)
      Wc.get_pristine_copy_path(path)
    end

    def default_ignores(config)
      Wc.get_default_ignores(config)
    end
    
    def cleanup(path, diff3_cmd=nil, cancel_func=nil)
      Wc.cleanup2(path, diff3_cmd, cancel_func)
    end

    AdmAccess = SWIG::TYPE_p_svn_wc_adm_access_t
    class AdmAccess
      class << self
        def open(associated, path, write_lock,
                 depth, cancel_func=nil, &block)
          _open(:adm_open3, associated, path, write_lock,
                depth, cancel_func, &block)
        end

        def probe_open(associated, path, write_lock, depth,
                       cancel_func=nil, &block)
          _open(:adm_probe_open3, associated, path, write_lock,
                depth, cancel_func, &block)
        end

        def open_anchor(path, write_lock, depth,
                        cancel_func=nil, &block)
          _open(:adm_open_anchor, path, write_lock, depth,
                cancel_func, &block)
        end

        private
        def _open(name, *args, &block)
          adm = Wc.__send__(name, *args, &block)
          
          if block_given?
            begin
              yield adm
            ensure
              adm.close
            end
          else
            adm
          end
        end
      end

      def open(*args, &block)
        self.class.open(self, *args, &block)
      end

      def probe_open(*args, &block)
        self.class.probe_open(self, *args, &block)
      end

      def retrieve(path)
        Wc.adm_retrieve(self, path)
      end
        
      def probe_retrieve(path)
        Wc.adm_probe_retrieve(self, path)
      end

      def probe_try(path, write_lock, depth, &cancel_func)
        Wc.adm_probe_try3(self, path, write_lock, depth, cancel_func)
      end

      def close
        Wc.adm_close(self)
      end

      def path
        Wc.adm_access_path(self)
      end

      def locked?
        Wc.adm_locked(self)
      end

      def has_binary_prop?(path)
        Wc.has_binary_prop(path, self)
      end

      def text_modified?(filename, force=false)
        Wc.text_modified_p(filename, force, self)
      end
      
      def props_modified?(path)
        Wc.props_modified_p(path, self)
      end

      def entry(path, show_hidden=false)
        Wc.entry(path, self, show_hidden, Svn::Core::Pool.new)
      end

      def read_entries(show_hidden=false)
        Wc.entries_read(self, show_hidden, Svn::Core::Pool.new)
      end

      def ancestry(path)
        Wc.get_ancestry(path, self)
      end

      def walk_entries(path, callbacks, show_hidden=false, cancel_func=nil)
        Wc.walk_entries2(path, self, callbacks, show_hidden, cancel_func)
      end

      def mark_missing_deleted(path)
        Wc.mark_missing_deleted(path, self)
      end

      def maybe_set_repos_root(path, repos)
        Wc.maybe_set_repos_root(self, path, repos)
      end
      
      def status(path)
        Wc.status2(path, self)
      end

      def status_editor(target, config, recurse=true,
                        get_all=true, no_ignore=true,
                        cancel_func=nil, traversal_info=nil)
        status_func = Proc.new do |path, status|
          yield(path, status)
        end
        ret = Wc.get_status_editor2(self, target, config, recurse,
                                    get_all, no_ignore, status_func,
                                    cancel_func, traversal_info)
        editor, editor_baton, set_lock_baton = *ret
        editor.instance_variable_set("@__status_fun__", status_func)
        editor.baton = editor_baton
        def set_lock_baton.set_repos_locks(locks, repos_root)
          Wc.status_set_repos_locks(self, locks, repos_root)
        end
        [editor, set_lock_baton]
      end

      def copy(src, dst_basename, cancel_func=nil, notify_func=nil)
        Wc.copy2(src, self, dst_basename, cancel_func, notify_func)
      end

      def delete(path, cancel_func=nil, notify_func=nil)
        Wc.delete2(path, self, cancel_func, notify_func)
      end

      def add(path, copyfrom_url=nil, copyfrom_rev=0,
              cancel_func=nil, notify_func=nil)
        Wc.add2(path, self, copyfrom_url, copyfrom_rev,
                cancel_func, notify_func)
      end

      def add_repos_file(dst_path, new_text_path, new_props,
                         copyfrom_url=nil, copyfrom_rev=0)
        Wc.add_repos_file(dst_path, self, new_text_path,
                          new_props, copyfrom_url, copyfrom_rev)
      end

      def add_repos_file2(dst_path, new_text_base_path, new_base_props,
                          new_text_path=nil, new_props=nil,
                          copyfrom_url=nil, copyfrom_rev=0)
        Wc.add_repos_file2(dst_path, self,
                           new_text_base_path, new_text_path,
                           new_base_props, new_props,
                           copyfrom_url, copyfrom_rev)
      end

      def remove_from_revision_control(name, destroy_wf=true,
                                       instant_error=true,
                                       cancel_func=nil)
        Wc.remove_from_revision_control(self, name,
                                        destroy_wf,
                                        instant_error,
                                        cancel_func)
      end

      def resolved_conflict(path, resolve_text=true,
                            resolve_props=true, recurse=true,
                            notify_func=nil, cancel_func=nil)
        Wc.resolved_conflict2(path, self, resolve_text,
                              resolve_props, recurse,
                              notify_func, cancel_func)
      end

      def process_committed(path, new_revnum, rev_date=nil, rev_author=nil,
                            wcprop_changes=[], recurse=true,
                            remove_lock=true, digest=nil)
        Wc.process_committed3(path, self, recurse,
                              new_revnum, rev_date,
                              rev_author, wcprop_changes,
                              remove_lock, digest)
      end

      def crawl_revisions(path, reporter, restore_files=true,
                          recurse=true, use_commit_times=true,
                          notify_func=nil, traversal_info=nil)
        Wc.crawl_revisions2(path, self, reporter, reporter.baton,
                            restore_files, recurse, use_commit_times,
                            notify_func, traversal_info)
      end

      def wc_root?(path)
        Wc.is_wc_root(path, self)
      end

      def update_editor(target, use_commit_times=true, recurse=true,
                        diff3_cmd=nil, notify_func=nil, cancel_func=nil,
                        traversal_info=nil)
        editor, editor_baton = Wc.get_update_editor2(target, self,
                                                     use_commit_times, recurse,
                                                     notify_func, cancel_func,
                                                     diff3_cmd, traversal_info)
        editor.baton = editor_baton
        editor
      end

      def switch_editor(target, switch_url, use_commit_times=true,
                        recurse=true, diff3_cmd=nil, notify_func=nil,
                        cancel_func=nil, traversal_info=nil)
        editor, editor_baton = Wc.get_update_editor2(target, switch_url,
                                                     self, use_commit_times,
                                                     recurse, notify_func,
                                                     cancel_func, diff3_cmd,
                                                     traversal_info)
        editor.baton = editor_baton
        editor
      end

      def prop_list(path)
        Wc.prop_list(path, self)
      end

      def prop(name, path)
        Wc.prop_get(name, path, self)
      end

      def set_prop(name, value, path, skip_checks=false)
        Wc.prop_set2(name, value, path, self, skip_checks)
      end

      def diff_editor(target, callbacks, recurse=true,
                      ignore_ancestry=true, use_text_base=false,
                      reverse_order=false, cancel_func=nil)
        editor, editor_baton = Wc.get_diff_editor3(target, self, callbacks,
                                                   recurse, ignore_ancestry,
                                                   use_text_base, reverse_order,
                                                   cancel_func)
        editor.baton = editor_baton
        editor
      end

      def diff(target, callbacks, recurse=true, ignore_ancestry=true)
        Wc.diff3(self, target, callbacks, recurse, ignore_ancestry)
      end

      def prop_diffs(path)
        Wc.get_prop_diffs(path, self)
      end

      def merge(left, right, merge_target, left_label,
                right_label, target_label, dry_run=false,
                diff3_cmd=nil, merge_options=nil)
        Wc.merge2(left, right, merge_target, self,
                  left_label, right_label, target_label,
                  dry_run, diff3_cmd, merge_options)
      end

      def merge_props(path, baseprops, propchanges, base_merge=true,
                      dry_run=false)
        Wc.merge_props(path, self, baseprops, propchanges,
                      base_merge, dry_run)
      end

      def merge_prop_diffs(path, propchanges, base_merge=true,
                           dry_run=false)
        Wc.merge_prop_diffs(path, self, propchanges,
                           base_merge, dry_run)
      end

      def relocate(path, from, to, recurse=true, validator=nil)
        Wc.relocate2(path, self, from, to, recurse, validator)
      end

      def revert(path, recurse=true, use_commit_times=true,
                 cancel_func=nil, notify_func=nil)
        Wc.revert2(path, self, recurse, use_commit_times,
                   cancel_func, notify_func)
      end

      def translated_file(src, versioned_file, flags)
        Wc.translated_file2(src, versioned_file, self, flags)
      end

      def transmit_text_deltas(path, editor, file_baton, fulltext=false)
        editor.baton = file_baton
        Wc.transmit_text_deltas(path, self, fulltext, editor)
      end

      def transmit_text_deltas2(path, editor, fulltext=false)
        Wc.transmit_text_deltas2(path, self, fulltext, editor)
      end

      def transmit_prop_deltas(path, entry, editor, baton=nil)
        editor.baton = baton if baton
        Wc.transmit_prop_deltas(path, self, entry, editor)
      end

      def ignores(config)
        Wc.get_ignores(config, self)
      end

      def add_lock(path, lock)
        Wc.add_lock(path, lock, self)
      end

      def remove_lock(path)
        Wc.remove_lock(path, self)
      end
    end

    TraversalInfo = SWIG::TYPE_p_svn_wc_traversal_info_t
    
    class TraversalInfo
      class << self
        def new
          Wc.init_traversal_info
        end
      end

      def edited_externals
        Wc.edited_externals(self)
      end
    end

    class Entry

      class << self
        def new(path, adm_access, show_hidden)
          Wc.entry(path, adm_access, show_hidden)
        end
      end
      
      def dup
        Wc.entry_dup(self, Svn::Core::Pool.new)
      end

      def conflicted(dir_path)
        Wc.conflicted_p(dir_path, self)
      end

      def conflicted?(dir_path)
        conflicted(dir_path).any? {|x| x}
      end

      def text_conflicted?(dir_path)
        conflicted(dir_path)[0]
      end
      
      def prop_conflicted?(dir_path)
        conflicted(dir_path)[1]
      end
      
      def dir?
        kind == Core::NODE_DIR
      end

      def file?
        kind == Core::NODE_FILE
      end

      def add?
        schedule == SCHEDULE_ADD
      end

      def normal?
        schedule == SCHEDULE_NORMAL
      end
    end
    
    class Status2
      def dup
        Wc.dup_status2(self, Core::Pool.new)
      end
      
      def text_added?
        text_status == STATUS_ADDED
      end

      def text_normal?
        text_status == STATUS_NORMAL
      end
    end

    class Notify
      class << self
        def new(path, action)
          Wc.create_notify(path, action)
        end
      end

      def dup
        Wc.dup_notify(self, Core::Pool.new)
      end
      
      def commit_added?
        action == NOTIFY_COMMIT_ADDED
      end

      def commit_deleted?
        action == NOTIFY_COMMIT_DELETED
      end

      def commit_postfix_txdelta?
        action == NOTIFY_COMMIT_POSTFIX_TXDELTA
      end

      def add?
        action == NOTIFY_ADD
      end

      def locked?
        lock_state = NOTIFY_LOCK_STATE_LOCKED
      end

      def unlocked?
        lock_state = NOTIFY_LOCK_STATE_UNLOCKED
      end
    end

    class RevisionStatus
      class << self
        undef new
        def new(wc_path, trail_url, committed, cancel_func=nil)
          Wc.revision_status(wc_path, trail_url, committed, cancel_func)
        end
      end
    end
  end
end
