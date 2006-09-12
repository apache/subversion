#!/usr/bin/env ruby

require "test/unit"
require "fileutils"

ENV["PATH"] = File.join(Dir.pwd, "..", "..", "..", "svnserve") + ":" + ENV["PATH"]
ext_dir = File.join(Dir.pwd, ".ext")
ext_svn_dir = File.join(ext_dir, "svn")
FileUtils.mkdir_p(ext_svn_dir)
FileUtils.ln_sf(File.join(Dir.pwd, ".libs"), File.join(ext_svn_dir, "ext"))
at_exit {FileUtils.rm_rf(ext_dir)}

$LOAD_PATH.unshift(ext_dir)
$LOAD_PATH.unshift(Dir.pwd)

begin
  require "gettext"
  Locale.setlocale(Locale::ALL, nil)
rescue LoadError
end

if Test::Unit::AutoRunner.respond_to?(:standalone?)
  exit Test::Unit::AutoRunner.run($0, File.dirname($0))
else
  exit Test::Unit::AutoRunner.run(false, File.dirname($0))
end
