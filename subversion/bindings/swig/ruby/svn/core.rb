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

    # for backward compatibility
    SWIG_INVALID_REVNUM = INVALID_REVNUM
    SWIG_IGNORED_REVNUM = IGNORED_REVNUM

    class << self
      alias binary_mime_type? mime_type_is_binary
    end


    DEFAULT_CHARSET = default_charset
    LOCALE_CHARSET = locale_charset
    
    AuthCredSSLClientCert = AuthCredSslClientCert
    AuthCredSSLClientCertPw = AuthCredSslClientCertPw
    AuthCredSSLServerTrust = AuthCredSslServerTrust

    dirent_all = 0
    constants.each do |name|
      dirent_all |= const_get(name) if /^DIRENT_/ =~ name
    end
    DIRENT_ALL = dirent_all

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
      if Core.const_defined?(:STREAM_CHUNK_SIZE)
        CHUNK_SIZE = Core::STREAM_CHUNK_SIZE
      else
        CHUNK_SIZE = 8192
      end

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
      attr_accessor :original, :modified, :latest, :ancestor

      class << self
        def version
          Core.diff_version
        end

        def file_diff(original, modified, options=nil)
          options ||= Core::DiffFileOptions.new
          diff = Core.diff_file_diff_2(original, modified, options)
          if diff
            diff.original = original
            diff.modified = modified
          end
          diff
        end

        def file_diff3(original, modified, latest, options=nil)
          options ||= Core::DiffFileOptions.new
          diff = Core.diff_file_diff3_2(original, modified, latest, options)
          if diff
            diff.original = original
            diff.modified = modified
            diff.latest = latest
          end
          diff
        end

        def file_diff4(original, modified, latest, ancestor, options=nil)
          options ||= Core::DiffFileOptions.new
          args = [original, modified, latest, ancestor, options]
          diff = Core.diff_file_diff4_2(*args)
          if diff
            diff.original = original
            diff.modified = modified
            diff.latest = latest
            diff.ancestor = ancestor
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

    class DiffFileOptions
      class << self
        undef new
        def new(*args)
          options = Svn::Core.diff_file_options_create(*args)
          options.__send__("initialize", *args)
          options
        end

        def parse(*args)
          options = new
          options.parse(*args)
          options
        end
      end

      def parse(*args)
        args = args.first if args.size == 1 and args.first.is_a?(Array)
        Svn::Core.diff_file_options_parse(self, args)
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

    # Following methods are also available:
    #
    # [created_rev]
    #   Returns a revision at which the instance was last modified.
    # [have_props?]
    #   Returns +true+ if the instance has properties.
    # [last_author]
    #   Returns an author who last modified the instance.
    # [size]
    #   Returns a size of the instance.
    class Dirent
      alias have_props? has_props

      # Returns +true+ when the instance is none.
      def none?
        kind == NODE_NONE
      end

      # Returns +true+ when the instance is a directory.
      def directory?
        kind == NODE_DIR
      end

      # Returns +true+ when the instance is a file.
      def file?
        kind == NODE_FILE
      end

      # Returns +true+ when the instance is an unknown node.
      def unknown?
        kind == NODE_UNKNOWN
      end

      # Returns a Time when the instance was last changed.
      #
      # Svn::Core::Dirent#time is replaced by this method, _deprecated_,
      # and provided for backward compatibility with the 1.3 API.
      def time2
        __time = time
        __time && Time.from_apr_time(__time)
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

    # Following methods are also available:
    #
    # [action]
    #   Returns an action taken to the path at the revision.
    # [copyfrom_path]
    #   If the path was added at the revision by the copy action from
    #   another path at another revision, returns an original path.
    #   Otherwise, returns +nil+.
    # [copyfrom_rev]
    #   If the path was added at the revision by the copy action from
    #   another path at another revision, returns an original revision.
    #   Otherwise, returns <tt>-1</tt>.
    class LogChangedPath
      # Returns +true+ when the path is added by the copy action.
      def copied?
        Util.copy?(copyfrom_path, copyfrom_rev)
      end
    end
  end
end
