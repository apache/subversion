#!/usr/bin/env ruby

require "test/unit"
require "fileutils"

ext_dir = File.join(Dir.pwd, ".ext")
ext_svn_dir = File.join(ext_dir, "svn")
FileUtils.mkdir_p(ext_svn_dir)
FileUtils.ln_sf(File.join(Dir.pwd, ".libs"), File.join(ext_svn_dir, "ext"))
at_exit {FileUtils.rm_rf(ext_dir)}

$LOAD_PATH.unshift(ext_dir)
$LOAD_PATH.unshift(Dir.pwd)

exit Test::Unit::AutoRunner.run(false, File.dirname($0))
