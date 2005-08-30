require "English"
require "svn/error"
require "svn/util"
require "svn/core"
require "svn/repos"
require "svn/ext/ra"

module Svn
  module Ra
    Util.set_constants(Ext::Ra, self)
    Util.set_methods(Ext::Ra, self)

    @@ra_pool = Svn::Core::Pool.new
    Ra.initialize(@@ra_pool)

    class << self
      def modules
        print_modules("")
      end
    end
    
    Session = SWIG::TYPE_p_svn_ra_session_t

    class Session
      class << self
        def open(url, config={}, callbacks=nil)
          Ra.open2(url, callbacks, config)
        end
      end

      def latest_revnum
        Ra.get_latest_revnum(self)
      end

      def dated_revision(time)
        Ra.get_dated_revision(self, time.to_apr_time)
      end

      def set_rev_prop(name, value, rev=nil)
        Ra.change_rev_prop(self, rev || latest_revnum, name, value)
      end

      def rev_proplist(name, rev=nil)
        Ra.rev_proplist(self, rev || latest_revnum)
      end

      def rev_prop(name, rev=nil)
        Ra.rev_prop(self, rev || latest_revnum, name)
      end

      def commit_editor(log_msg, lock_tokens={}, keep_lock=false)
        callback = Proc.new do |new_revision, date, author|
          date = Time.from_svn_format(date) if date
          yield(new_revision, date, author)
        end
        editor, editor_baton = Ra.get_commit_editor(log_msg, callback,
                                                    lock_tokens, keep_lock)
      end
      
      def file(path, rev=nil)
        output = StringIO.new
        *rv = Ra.get_file(self, path, rev || latest_revnum, output)
        output.rewind
        [output.read, *rv]
      end

      def dir(path, rev=nil)
        Ra.get_dir(self, path, rev || latest_revnum)
      end

      def update(revision_to_update_to,	update_target,
                 editor, editor_baton, recurse=true)
        reporter, reporter_baton = Ra.do_update(self, revision_to_update_to,
                                                update_target, editor)
        reporter.baton = reporter_baton
        if block_given?
          yield(reporter)
        else
          reporter.finish_report
        end
      end

      def switch(revision_to_switch_to,	switch_target, switch_url,
                 editor, editor_baton, recurse=true)
        reporter, reporter_baton = Ra.do_switch(self, revision_to_switch_to,
                                                switch_target, recurse,
                                                switch_url, editor,
                                                editor_baton)
        reporter.baton = reporter_baton
        if block_given?
          yield(reporter)
        else
          reporter.finish_report
        end
      end

      def status(revision, status_target, editor, editor_baton, recurse=true)
        reporter, reporter_baton = Ra.do_status(self, status_target,
                                                revision, recurse, editor,
                                                editor_baton)
        
        reporter.baton = reporter_baton
        if block_given?
          yield(reporter)
        else
          reporter.finish_report
        end
      end

      def status(revision, diff_target, versus_url, editor,
                 editor_baton, recurse=true, ignore_ancestry=false)
        reporter, reporter_baton = Ra.do_diff(self, revision,
                                              diff_target, recurse,
                                              ignore_ancestry, versus_url,
                                              editor, editor_baton)
        reporter.baton = reporter_baton
        if block_given?
          yield(reporter)
        else
          reporter.finish_report
        end
      end

      def log(paths, start_rev, end_rev, limit,
              discover_changed_paths=true,
              strict_node_history=false)
        paths = [paths] unless paths.is_a?(Array)
        receiver = Proc.new do |changed_paths, revision, author, date, message|
          date = Util.string_to_time(date) if date
          yield(changed_paths, revision, author, date, message)
        end
        Ra.get_log(self, paths, start_rev, end_rev, limit,
                   discover_changed_paths, strict_node_history,
                   receiver)
      end

      def check_path(path, rev=nil)
        Ra.check_path(self, path, rev || latest_revnum)
      end

      def stat(path, rev=nil)
        Ra.stat(self, path, rev || latest_revnum)
      end

      def uuid
        Ra.uuid(self)
      end

      def repos_root
        Ra.get_repos_root(self)
      end

      def locations(path, location_revisions, peg_revision=nil)
        peg_revision ||= latest_revnum
        Ra.get_locations(self, path, peg_revision, location_revisions)
      end

      def file_revs(path, start_rev, end_rev=nil)
        end_rev ||= latest_revnum
        handler = Proc.new do |path, rev, rev_props, prop_diffs|
          yield(path, rev, rev_props, prop_diffs)
        end
        Ra.get_file_revs(self, path, start_rev, end_rev, handler)
      end

      def lock(path_revs, comment=nil, steal_lock=false)
        lock_func = Proc.new do |path, do_lock, lock, ra_err|
          yield(path, do_lock, lock, ra_err)
        end
        Ra.lock(self, path_revs, comment, steal_lock, lock_func)
      end

      def unlock(path_tokens, break_lock=false)
        lock_func = Proc.new do |path, do_lock, lock, ra_err|
          yield(path, do_lock, lock, ra_err)
        end
        Ra.unlock(self, path_tokens, break_lock, lock_func)
      end

      def get_lock(path)
        Ra.get_lock(self, path)
      end

      def get_locks(path)
        Ra.get_locks(self, path)
      end
    end

    class Reporter2
      attr_accessor :baton

      %w(set_path delete_path link_path finish_report
         abort_report).each do |target|
        alias_method("_#{target}", target)
      end

      def set_path(path, revision, start_empty=true, lock_token=nil)
        _set_path(@baton, path, revision, start_empty, lock_token)
      end
      
      def delete_path(path)
        _set_path(@baton, path)
      end
      
      def link_path(path, url, revision, start_empty=true, lock_token=nil)
        _link_path(@baton, path, url, revision,
                   start_empty, lock_token)
      end

      def finish_report
        _finish_report(@baton)
      end

      def abort_report
        _abort_report(@baton)
      end
      
    end
  end
end
