require "English"
require "time"
require "stringio"
require "svn/error"
require "svn/util"
require "svn/ext/core"

class Time
  MILLION = 1000000

  class << self
    def from_apr_time(apr_time)
      sec, usec = apr_time.divmod(MILLION)
      Time.at(sec, usec)
    end

    def from_svn_format(str)
      from_apr_time(Svn::Core.time_from_cstring(str))
    end

    def parse_svn_format(str)
      matched, result = Svn::Core.parse_date(str, Time.now.to_apr_time)
      if matched
        from_apr_time(result)
      else
        nil
      end
    end
  end
  
  def to_apr_time
    to_i * MILLION + usec
  end

  def to_svn_format
    Svn::Core.time_to_cstring(self.to_apr_time)
  end

  def to_svn_human_format
    Svn::Core.time_to_human_cstring(self.to_apr_time)
  end
end

module Svn
  module Core
    Util.set_constants(Ext::Core, self)
    Util.set_methods(Ext::Core, self)

    apr_initialize
    at_exit do
      if $DEBUG
        i = 0
        loop do
          i += 1
          print "number of pools before GC(#{i}): "
          before_pools = Svn::Core::Pool.number_of_pools
          p before_pools
          GC.start
          after_pools = Svn::Core::Pool.number_of_pools
          print "number of pools after GC(#{i}): "
          p after_pools
          break if before_pools == after_pools
        end
        puts "GC ran #{i} times"
      end
      
      # We don't need to call apr_termintae because pools
      # are destroyed by ruby's GC.
      # Svn::Core.apr_terminate
    end
    nls_init
    
    class << self
      alias binary_mime_type? mime_type_is_binary
    end


    DEFAULT_CHARSET = default_charset
    LOCALE_CHARSET = locale_charset
    
    AuthCredSSLClientCert = AuthCredSslClientCert
    AuthCredSSLClientCertPw = AuthCredSslClientCertPw
    AuthCredSSLServerTrust = AuthCredSslServerTrust
    
    
    Pool = Svn::Ext::Core::Apr_pool_wrapper_t
    
    class Pool
      class << self
        def number_of_pools
          ObjectSpace.each_object(Pool) {}
        end
      end

      alias _initialize initialize
      private :_initialize
      def initialize(parent=nil)
        _initialize(parent)
        @parent = parent
      end
    end

    Stream = SWIG::TYPE_p_svn_stream_t

    class Stream
      CHUNK_SIZE = Core::STREAM_CHUNK_SIZE

      def write(data)
        Core.stream_write(self, data)
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
        Core.stream_close(self)
      end

      def copy(other)
        Core.stream_copy(self, other)
      end
      
      private
      def _read(size)
        Core.stream_read(self, size)
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
        def new(providers=[], *rest)
          baton = Core.auth_open(providers)
          baton.__send__("initialize", providers, *rest)
          baton
        end
      end

      attr_reader :parameters
      def initialize(providers, parameters={})
        @providers = providers
        self.parameters = parameters
      end

      def [](name)
        Core.auth_get_parameter(self, name)
      end

      def []=(name, value)
        Core.auth_set_parameter(self, name, value)
        @parameters[name] = value
      end

      def parameters=(params)
        @parameters = {}
        params.each do |key, value|
          self[key] = value
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
      attr_accessor :original, :modified, :latest

      class << self
        def version
          Core.diff_version
        end

        def file_diff(original, modified)
          diff = Core.diff_file_diff(original, modified)
          if diff
            diff.original = original
            diff.modified = modified
          end
          diff
        end

        def file_diff3(original, modified, latest)
          diff = Core.diff_file_diff3(original, modified, latest)
          if diff
            diff.original = original
            diff.modified = modified
            diff.latest = latest
          end
          diff
        end
      end
      
      def unified(orig_label, mod_label, header_encoding=nil)
        header_encoding ||= Svn::Core.locale_charset
        output = StringIO.new
        args = [
          output, self, @original, @modified,
          orig_label, mod_label, header_encoding
        ]
        Core.diff_file_output_unified2(*args)
        output.rewind
        output.read
      end

      def merge(conflict_original=nil, conflict_modified=nil,
                conflict_latest=nil, conflict_separator=nil,
                display_original_in_conflict=true,
                display_resolved_conflicts=true)
        header_encoding ||= Svn::Core.locale_charset
        output = StringIO.new
        args = [
          output, self, @original, @modified, @latest,
          conflict_original, conflict_modified,
          conflict_latest, conflict_separator,
          display_original_in_conflict,
          display_resolved_conflicts,
        ]
        Core.diff_file_output_merge(*args)
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

    class Version

      alias _initialize initialize
      def initialize(major=nil, minor=nil, patch=nil, tag=nil)
        _initialize
        self.major = major if major
        self.minor = minor if minor
        self.patch = patch if patch
        self.tag = tag || ""
      end

      def ==(other)
        valid? and other.valid? and Core.ver_equal(self, other)
      end

      def compatible?(other)
        valid? and other.valid? and Core.ver_compatible(self, other)
      end

      def valid?
        (major and minor and patch and tag) ? true : false
      end

      alias _tag= tag=
      def tag=(value)
        @tag = value
        self._tag = value
      end

      def to_a
        [major, minor, patch, tag]
      end
      
      def to_s
        "#{major}.#{minor}.#{patch}#{tag}"
      end
    end

    class Dirent
      def directory?
        kind == NODE_DIR
      end

      def file?
        kind == NODE_FILE
      end
    end

    Config = SWIG::TYPE_p_svn_config_t
    
    class Config
      class << self
        def config(path)
          Core.config_get_config(path)
        end

        def read(file, must_exist=true)
          Core.config_read(file, must_exist)
        end

        def ensure(dir)
          Core.config_ensure(dir)
        end

        def read_auth_data(cred_kind, realm_string, config_dir=nil)
          Core.config_read_auth_data(cred_kind, realm_string, config_dir)
        end

        def write_auth_data(hash, cred_kind, realm_string, config_dir=nil)
          Core.config_write_auth_data(hash, cred_kind,
                                      realm_string, config_dir)
        end
      end

      def merge(file, must_exist=true)
        Core.config_merge(self, file, must_exist)
      end

      def get(section, option, default=nil)
        Core.config_get(self, section, option, default)
      end
      
      def get_bool(section, option, default)
        Core.config_get_bool(self, section, option, default)
      end

      def set(section, option, value)
        Core.config_set(self, section, option, value)
      end
      
      def set_bool(section, option, value)
        Core.config_set_bool(self, section, option, value)
      end

      def each_option(section)
        receiver = Proc.new do |name, value|
          yield(name, value)
        end
        Core.config_enumerate2(self, section, receiver)
      end

      def each_section
        receiver = Proc.new do |name|
          yield(name)
        end
        Core.config_enumerate_sections2(self, receiver)
      end

      def find_group(key, section)
        Core.config_find_group(self, key, section)
      end

      def get_server_setting(group, name, default=nil)
        Core.config_get_server_setting(self, group, name, default)
      end

      def get_server_setting_int(group, name, default)
        Core.config_get_server_setting_int(self, group, name, default)
      end
    end

    module Property
      module_function
      def kind(name)
        kind, len = Core.property_kind(name)
        [kind, name[0...len]]
      end

      def svn_prop?(name)
        Core.prop_is_svn_prop(name)
      end

      def needs_translation?(name)
        Core.prop_needs_translation(name)
      end

      def categorize_props(props)
        Core.categorize_props(props)
      end

      def prop_diffs(target_props, source_props)
        Core.prop_diffs(target_props, source_props)
      end
    end

    class CommitInfo
      class << self
        undef new
        def new
          info = Core.create_commit_info
          info.__send__("initialize")
          info
        end
      end
      
      alias _date date
      def date
        __date = _date
        __date && Time.from_svn_format(__date)
      end
    end
  end
end
