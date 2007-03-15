require 'fileutils'

module SvnTestUtil
  module Windows
    module Svnserve
      begin
        require 'win32/service'

        SERVICE_NAME = 'test-svn-server'

        def setup_svnserve
          @svnserve_port = @svnserve_ports.first
          @repos_svnserve_uri = "svn://#{@svnserve_host}:#{@svnserve_port}"

          unless Win32::Service.exists?(SERVICE_NAME)
            # Here we assume that svnserve is going available on the path when
            # the service starts.  So use "svnserve" unqualified.  This isn't
            # normally how I'd recommend installing a windows service, but for
            # running these tests it is a significantly simplifying assumption.
            # We can't even test for svnserve being on the path here because
            # when the service starts, it'll be running as LocalSystem and the
            # new process may actually have a different path than we have here.

            Win32::Service.new.create_service do |s|
              s.service_name = SERVICE_NAME
              root = @full_repos_path.tr('/','\\')
              s.binary_path_name = "svnserve"
              s.binary_path_name << " --service"
              s.binary_path_name << " --root \"#{root}\""
              s.binary_path_name << " --listen-host #{@svnserve_host}"
              s.binary_path_name << " --listen-port #{@svnserve_port}"
            end.close
            at_exit{Win32::Service.delete(SERVICE_NAME)}
          end

          Win32::Service.start(SERVICE_NAME)
        end

        def teardown_svnserve
          Win32::Service.stop(SERVICE_NAME) rescue Win32::ServiceError
        end
      rescue LoadError
        puts "Testing with file:// instead of svn://."
        puts "Install win32-service to enable testing with svnserve."

        def setup_svnserve
          @repos_svnserve_uri = @repos_uri
        end

        def teardown_svnserve
        end
      end

      def add_pre_revprop_change_hook
        File.open("#{@repos.pre_revprop_change_hook}.cmd", "w") do |hook|
          hook.print <<-HOOK
set REPOS=%1
set REV=%2
set USER=%3
set PROPNAME=%4
if "%PROPNAME%" == "#{Svn::Core::PROP_REVISION_LOG}" if "%USER%" == "#{@author}" exit 0
exit 1
          HOOK
        end
      end
    end

    module SetupEnvironment
      def setup_test_environment(top_dir, base_dir, ext_dir)
        build_type = "Release"

        FileUtils.mkdir_p(ext_dir)

        relative_base_dir =
          base_dir.sub(/^#{Regexp.escape(top_dir + File::SEPARATOR)}/, '')
        build_base_dir = File.join(top_dir, build_type, relative_base_dir)

        dll_dir = File.expand_path(build_base_dir)
        subversion_dir = File.join(build_base_dir, "..", "..", "..")
        subversion_dir = File.expand_path(subversion_dir)

        util_name = "util"
        build_conf = File.join(top_dir, "build.conf")
        File.open(File.join(ext_dir, "#{util_name}.rb" ), 'w') do |util|
          setup_dll_wrapper_util(dll_dir, util)
          add_apr_dll_path_to_dll_wrapper_util(top_dir, build_type, util)
          add_svn_dll_path_to_dll_wrapper_util(build_conf, subversion_dir, util)
          setup_dll_wrappers(build_conf, ext_dir, dll_dir, util_name) do |lib|
            svn_lib_dir = File.join(subversion_dir, "libsvn_#{lib}")
            util.puts("add_path.call(#{svn_lib_dir.dump})")
          end
        end
      end

      private
      def setup_dll_wrapper_util(dll_dir, util)
        libsvn_swig_ruby_dll_dir = File.join(dll_dir, "libsvn_swig_ruby")

        util.puts(<<-EOC)
paths = ENV["PATH"].split(';')
add_path = Proc.new do |path|
  win_path = path.tr(File::SEPARATOR, File::ALT_SEPARATOR)
  unless paths.include?(path)
    ENV["PATH"] = "\#{path};\#{ENV['PATH']}"
  end
end

add_path.call(#{dll_dir.dump})
add_path.call(#{libsvn_swig_ruby_dll_dir.dump})
EOC
      end

      def add_apr_dll_path_to_dll_wrapper_util(top_dir, build_type, util)
        lines = []
        gen_make_opts = File.join(top_dir, "gen-make.opts")
        lines = File.read(gen_make_opts).to_a if File.exists?(gen_make_opts)
        config = {}
        lines.each do |line|
          name, value = line.split(/\s*=\s*/, 2)
          config[name] = value if value
        end

        ["apr", "apr-util", "apr-iconv"].each do |lib|
          lib_dir = config["--with-#{lib}"] || lib
          dll_dir = File.expand_path(File.join(top_dir, lib_dir, build_type))
          util.puts("add_path.call(#{dll_dir.dump})")
        end
      end

      def add_svn_dll_path_to_dll_wrapper_util(build_conf, subversion_dir, util)
        File.open(build_conf) do |f|
          f.each do |line|
            if /^\[(libsvn_.+)\]\s*$/ =~ line
              lib_name = $1
              lib_dir = File.join(subversion_dir, lib_name)
              util.puts("add_path.call(#{lib_dir.dump})")
            end
          end
        end
      end

      def setup_dll_wrappers(build_conf, ext_dir, dll_dir, util_name)
        File.open(build_conf) do |f|
          f.each do |line|
            if /^\[swig_(.+)\]\s*$/ =~ line
              lib_name = $1
              File.open(File.join(ext_dir, "#{lib_name}.rb" ), 'w') do |rb|
                rb.puts(<<-EOC)
require File.join(File.dirname(__FILE__), #{util_name.dump})
require File.join(#{dll_dir.dump}, File.basename(__FILE__, '.rb')) + '.so'
EOC
              end

              yield(lib_name)
            end
          end
        end
      end
    end
  end
end
