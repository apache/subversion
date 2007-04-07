# -*- ruby -*-

def usage
  puts "#{$0} target_directory"
end

if ARGV.size < 1
  usage
  exit 1
end

target_dir = ARGV.shift

if target_dir == "--help"
  usage
  exit
end

base_dir = File.expand_path(File.join(target_dir, "ruby"))
$LOAD_PATH.unshift(File.join(base_dir, "ext"))
$LOAD_PATH.unshift(File.join(base_dir, "lib"))

require 'svn/core'

gem_file = nil
Dir.chdir(target_dir) do
  require 'rubygems'
  Gem.manage_gems

  spec = Gem::Specification.new do |s|
    s.name = "subversion"
    s.date = Time.now
    s.version = Svn::Core::VER_NUM
    s.summary = "The Ruby bindings for Subversion."
    s.email = "dev@subversion.tigris.org"
    s.homepage = "http://subversion.tigris.org/"
    s.description = s.summary
    s.authors = ["Kouhei Sutou"]
    s.files = Dir.glob(File.join("**", "*")).delete_if {|x| /\.gem$/i =~ x}
    s.require_paths = ["ruby/ext", "ruby/lib"]
    s.platform = Gem::Platform::WIN32
    s.required_ruby_version = '>= 1.8.2'
  end

  gem_file = File.expand_path(Gem::Builder.new(spec).build)
end
FileUtils.mv(gem_file, File.basename(gem_file))
