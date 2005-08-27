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

    alias locked? locked
    module_function :locked?

    
    AdmAccess = SWIG::TYPE_p_svn_wc_adm_access_t
    class AdmAccess
      class << self
        def open(associated, path, write_lock, depth)
          adm = Wc.adm_open2(associated, path, write_lock, depth)
          
          if block_given?
            ret = yield adm
            adm.close
            ret
          else
            adm
          end
        end
      end

      def close
        Wc.adm_close(self)
      end
      
      def status(path)
        Wc.status(path, self)
      end
    end

    class Entry
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
      def text_added?
        text_status == STATUS_ADDED
      end

      def text_normal?
        text_status == STATUS_NORMAL
      end
    end

    class Notify
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
    
  end
end
