require "English"
require "svn/error"
require "svn/util"
require "svn/ext/_ra"

module Svn
  module Ra
    Util.set_constants(Ext::Ra, self)
    Util.set_methods(Ext::Ra, self)
  end
end
