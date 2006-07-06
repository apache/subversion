require "svn/ext/core"

module Svn
  class Error < StandardError

    TABLE = {}

    Ext::Core.constants.each do |const_name|
      if /^SVN_ERR_(.*)/ =~ const_name
        error_const_name = $1
        next if /_CATEGORY_START\z/ =~ error_const_name
        value = Ext::Core.const_get(const_name)
        module_eval(<<-EOC, __FILE__, __LINE__ + 1)
          class #{error_const_name} < Error
            def initialize(message="", file=nil, line=nil)
              super(#{value}, message, file, line)
            end
          end
        EOC
        TABLE[value] = const_get(error_const_name)
      end
    end
    
    class << self
      def new_corresponding_error(code, message, file=nil, line=nil)
        if TABLE.has_key?(code)
          TABLE[code].new(message, file, line)
        else
          new(code, message, file, line)
        end
      end
    end

    attr_reader :code, :error_message, :file, :line
    def initialize(code, message, file=nil, line=nil)
      @code = code
      @error_message = to_locale_encoding(message)
      @file = file
      @line = line
      msg = ""
      if file
        msg << "#{file}"
        msg << ":#{line}" if line
        msg << " "
      end
      msg << @error_message
      super(msg)
    end

    private
    begin
      require "gettext"
      require "iconv"
      def to_locale_encoding(str)
        Iconv.iconv(Locale.charset, "UTF-8", str).join
      end
    rescue LoadError
      def to_locale_encoding(str)
        str
      end
    end
  end
end
