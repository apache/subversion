#!/usr/bin/env ruby

require "test/unit"
require "fileutils"

test_dir = File.expand_path(File.join(File.dirname(__FILE__)))
base_dir = File.expand_path(File.join(File.dirname(__FILE__), ".."))
top_dir = File.expand_path(File.join(base_dir, "..", "..", ".."))

ext_dir = File.join(base_dir, ".ext")
ext_svn_dir = File.join(ext_dir, "svn")
FileUtils.mkdir_p(ext_svn_dir)
at_exit {FileUtils.rm_rf(ext_dir)}

if /cygwin|mingw|mswin32|bccwin32/.match(RUBY_PLATFORM)
  ext_svn_ext_dir = File.join(ext_svn_dir, "ext")
  FileUtils.mkdir_p(ext_svn_ext_dir)
  FileUtils.cp(Dir.glob(File.join(base_dir, "*.dll"), ext_svn_ext_dir))
else
  ENV["PATH"] = "#{File.join(top_dir, 'subversion', 'svnserve')}:#{ENV['PATH']}"
  FileUtils.ln_sf(File.join(base_dir, ".libs"), File.join(ext_svn_dir, "ext"))
end

$LOAD_PATH.unshift(ext_dir)
$LOAD_PATH.unshift(base_dir)
$LOAD_PATH.unshift(test_dir)

require 'svn/core'
Svn::Locale.set

if Test::Unit::AutoRunner.respond_to?(:standalone?)
  exit Test::Unit::AutoRunner.run($0, File.dirname($0))
else
  exit Test::Unit::AutoRunner.run(false, File.dirname($0))
end
