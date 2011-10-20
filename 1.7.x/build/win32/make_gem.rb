#!/usr/bin/env ruby
#
#
#   Licensed to the Apache Software Foundation (ASF) under one
#   or more contributor license agreements.  See the NOTICE file
#   distributed with this work for additional information
#   regarding copyright ownership.  The ASF licenses this file
#   to you under the Apache License, Version 2.0 (the
#   "License"); you may not use this file except in compliance
#   with the License.  You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
#   Unless required by applicable law or agreed to in writing,
#   software distributed under the License is distributed on an
#   "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
#   KIND, either express or implied.  See the License for the
#   specific language governing permissions and limitations
#   under the License.
#
#

require 'optparse'
require 'ostruct'
require 'tmpdir'
require 'fileutils'

options = OpenStruct.new
options.output_dir = File.expand_path(Dir.pwd)

opts = OptionParser.new do |opts|
  opts.banner += " DIRECTORIES"
  opts.on("-oDIRECTORY", "--output-dir=DIRECTORY",
          "Output generated gem to DIRECTORY",
          "[#{options.output_dir}]") do |dir|
    options.output_dir = File.expand_path(dir)
  end

  opts.separator ""

  opts.on("-h", "--help", "Show this message") do
    puts opts
    exit
  end
end

target_dirs = opts.parse!(ARGV)
if target_dirs.empty?
  puts opts
  exit 1
end

target_dirs.each do |dir|
  next unless File.basename(dir) == "ruby"
  base_dir = File.expand_path(dir)
  $LOAD_PATH.unshift(File.join(base_dir, "ext"))
  $LOAD_PATH.unshift(File.join(base_dir, "lib"))
end

require 'svn/core'


archive_dir = File.join(Dir.tmpdir, "svn-ruby-gem-#{Process.pid}")
FileUtils.mkdir(archive_dir)
at_exit {FileUtils.rm_rf(archive_dir)}

target_dirs.each do |dir|
  FileUtils.cp_r(dir, archive_dir)
end


generated_gem_file = nil
Dir.chdir(archive_dir) do
  require 'rubygems'
  Gem.manage_gems

  spec = Gem::Specification.new do |s|
    s.name = "subversion"
    s.date = Time.now
    s.version = Svn::Core::VER_NUM
    s.summary = "The Ruby bindings for Subversion."
    s.email = "dev@subversion.apache.org"
    s.homepage = "http://subversion.apache.org/"
    s.description = s.summary
    s.authors = ["Kouhei Sutou"]
    s.files = Dir.glob(File.join("**", "*")).delete_if {|x| /\.gem$/i =~ x}
    s.require_paths = ["ruby/ext", "ruby/lib"]
    s.platform = Gem::Platform::WIN32
    s.required_ruby_version = '>= 1.8.2'
  end

  generated_gem_file = File.expand_path(Gem::Builder.new(spec).build)
end

gem_file = File.join(options.output_dir, File.basename(generated_gem_file))
FileUtils.mv(generated_gem_file, gem_file)
