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
        def open(associated, path, write_lock, depth, pool)
          adm = Util.set_pool(pool) do
            Wc.adm_open2(associated, path, write_lock, depth, pool)
          end
          
          if block_given?
            ret = yield adm
            adm.close
            ret
          else
            adm
          end
        end
      end

      attr_accessor :pool
      def close
        Wc.adm_close(self)
      end
      
      def status(path)
        Wc.status(path, self, @pool)
      end
    end
  end
end
