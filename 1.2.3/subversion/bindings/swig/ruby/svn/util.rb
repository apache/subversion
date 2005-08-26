require "time"

module Svn
  module Util

    MILLION = 1000000
    
    module_function
    def to_ruby_class_name(name)
      name.split("_").collect{|x| "#{x[0,1].upcase}#{x[1..-1]}"}.join("")
    end
      
    def to_ruby_const_name(name)
      name.upcase
    end

    def to_apr_time(value)
      if value.is_a?(Time)
        value.to_i * MILLION + value.usec
      else
        value
      end
    end

    def string_to_time(str, pool=nil)
      if pool
        sec, usec = Core.time_from_cstring(str, pool).divmod(MILLION)
        Time.at(sec, usec)
      else
        Time.parse(str).localtime
      end
    end

    def valid_rev?(rev)
      rev and rev >= 0
    end

    def copy?(copyfrom_path, copyfrom_rev)
      Util.valid_rev?(copyfrom_rev) && !copyfrom_path.nil?
    end
    
    def set_pool(pool)
      obj = yield
      obj.pool = pool unless obj.nil?
      obj
    end
    
    def set_constants(ext_mod, target_mod=self)
      target_name = nil
      ext_mod.constants.each do |const|
        target_name = nil
        case const
        when /^SVN_(?:#{target_mod.name.split("::").last.upcase}_)?/
          target_name = $POSTMATCH
        when /^SWIG_SVN_/
          target_name = "SWIG_#{$POSTMATCH}"
        when /^Svn_(?:#{target_mod.name.split("::").last.downcase}_)?(.+)_t$/
          target_name = to_ruby_class_name($1)
        when /^Svn_(?:#{target_mod.name.split("::").last.downcase}_)?/
          target_name = to_ruby_const_name($POSTMATCH)
#         else
#           puts const
        end
        unless target_name.nil?
          target_mod.const_set(target_name, ext_mod.const_get(const))
        end
      end
    end

    def set_methods(ext_mod, target_mod=self)
      target_name = nil
      ext_mod.public_methods(false).each do |meth|
        target_name = nil
        case meth
        when /^svn_(?:#{target_mod.name.split("::").last.downcase}_)?/
          target_name = $POSTMATCH
        when /^apr_/
          target_name = meth
        end
        unless target_name.nil?
          target_id = target_name.intern
          target_proc = ext_mod.method(meth).to_proc
          target_mod.__send__(:define_method, target_id, target_proc)
          target_mod.__send__(:module_function, target_id)
        end
      end
    end
  end
end
