require "English"
require "stringio"
require "svn/error"
require "svn/util"
require "svn/ext/core"

module Svn
  module Core
    Util.set_constants(Ext::Core, self)
    Util.set_methods(Ext::Core, self)

    apr_initialize
    at_exit {Svn::Core.apr_terminate}
    
    class << self

      alias pool_destroy apr_pool_destroy
      alias pool_clear apr_pool_clear
      alias binary_mime_type? mime_type_is_binary
    end


    AuthCredSSLClientCert = AuthCredSslClientCert
    AuthCredSSLClientCertPw = AuthCredSslClientCertPw
    AuthCredSSLServerTrust = AuthCredSslServerTrust
    
    
    Pool = SWIG::TYPE_p_apr_pool_t

    class Pool
      class << self
        def new(parent=nil)
          pool = Core.pool_create(parent)
          if block_given?
            result = yield pool
            pool.destroy
            result
          else
            pool
          end
        end
      end

      def clear
        Core.pool_clear(self)
      end
      
      def destroy
        Core.pool_destroy(self)
      end
    end

    
    Stream = SWIG::TYPE_p_svn_stream_t

    class Stream
      CHUNK_SIZE = Core::STREAM_CHUNK_SIZE

      attr_accessor :pool

      def write(data)
        Core.stream_close(self, @pool)
      end
      
      def read(len=nil)
        if len.nil?
          read_all
        else
          buf = ""
          while len > CHUNK_SIZE
            buf << _read(CHUNK_SIZE)
            len -= CHUNK_SIZE
          end
          buf << _read(len)
          buf
        end
      end
      
      def close
        Core.stream_close(self, @pool)
      end

      def copy(other)
        Core.stream_copy(self, other, @pool)
      end
      
      private
      def _read(size)
        Core.stream_read(self, size, @pool)
      end
      
      def read_all
        buf = ""
        while chunk = _read(CHUNK_SIZE)
          buf << chunk
        end
        buf
      end
    end


    AuthBaton = SWIG::TYPE_p_svn_auth_baton_t
    class AuthBaton
      class << self
        def open(providers, pool)
          Core.auth_open(providers, pool)
        end
      end
    end
    

    class AuthProviderObject
      class << self
        undef new
      end
    end


    Diff = SWIG::TYPE_p_svn_diff_t
    class Diff
      attr_accessor :pool
      attr_accessor :original, :modified

      class << self
        def file_diff(original, modified, pool)
          Util.set_pool(pool) do
            diff = Core.diff_file_diff(original, modified, pool)
            if diff
              diff.original = original
              diff.modified = modified
            end
            diff
          end
        end
      end
      
      def unified(orig_label, mod_label)
        output = StringIO.new
        args = [
          output, self, @original, @modified,
          orig_label, mod_label, @pool
        ]
        Core.diff_file_output_unified(*args)
        output.rewind
        output.read
      end

      def conflict?
        Core.diff_contains_conflicts(self)
      end

      def diff?
        Core.diff_contains_diffs(self)
      end
    end
  end
end
