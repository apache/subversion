require "svn/ext/core"

module Svn
  class Error < StandardError

    TABLE = {}

    Ext::Core.constants.each do |const_name|
      if /^SVN_ERR_(.*)/ =~ const_name
        value = Ext::Core.const_get(const_name)
        module_eval(<<-EOC, __FILE__, __LINE__)
          class #{$1} < Error
            def initialize(message)
              super(#{value}, message)
            end
          end
        EOC
        TABLE[value] = const_get($1)
      end
    end
    
    class << self
      def new_corresponding_error(code, message)
        if TABLE.has_key?(code)
          TABLE[code].new(message)
        else
          new(code, message)
        end
      end
    end

    attr_reader :code, :message
    def initialize(code, message)
      @code = code
      @message = message
      super(message)
    end
  end
end
