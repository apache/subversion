module Svn
  module Util

    @@wrapper_procs = []
    
    module_function
    def to_ruby_class_name(name)
      name.split("_").collect{|x| "#{x[0,1].upcase}#{x[1..-1]}"}.join("")
    end
      
    def to_ruby_const_name(name)
      name.upcase
    end

    def valid_rev?(rev)
      rev and rev >= 0
    end

    def copy?(copyfrom_path, copyfrom_rev)
      Util.valid_rev?(copyfrom_rev) && !copyfrom_path.nil?
    end
    
    def set_constants(ext_mod, target_mod=self)
      target_name = nil
      ext_mod.constants.each do |const|
        target_name = nil
        case const
        when /^SVN__/
          # ignore private constants
        when /^SVN_(?:#{target_mod.name.split("::").last.upcase}_)?/
          target_name = $POSTMATCH
        when /^SWIG_SVN_/
          target_name = $POSTMATCH
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
          target_method = ext_mod.method(meth)
          target_proc = Proc.new{|*args| target_method.call(*args)}
          target_mod.__send__(:define_method, target_id, target_proc)
          target_mod.__send__(:module_function, target_id)
          @@wrapper_procs << target_proc
        end
      end
    end
  end
end
