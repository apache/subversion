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

        svn_version_h = File.join(top_dir, "subversion", "include",
                                  "svn_version.h")
        version = nil
        File.open(svn_version_h) do |f|
          f.each do |line|
            if /^\s*#\s*define\s+SVN_VER_MAJOR\s+(\d+)/ =~ line
              version = $1
              break
            end
          end
        end

        # First we copy the ruby swig implementation library into the ext
        # directory. So we can make it available to the bindings themselves.
        # We assume that the libraries that libsvn_swig_ruby depends on will
        # be available on the path.
        relative_base_dir =
          base_dir.sub(/^#{Regexp.escape(top_dir + File::SEPARATOR)}/, '')
        build_base_dir = File.join(top_dir, build_type, relative_base_dir)
        libsvn_swig_ruby_name = 'libsvn_swig_ruby'
        dll = File.join(build_base_dir, libsvn_swig_ruby_name,
                        "#{libsvn_swig_ruby_name}-#{version}.dll")
        FileUtils.cp(dll, ext_dir)

        # Now we copy the actual swig dlls into the ext directory.  Since they
        # depend on libsvn_swig_ruby we have to provide a way for them to find
        # libsvn_swig_ruby.  Since a require will try to load a .rb before a
        # .dll or .so, we generate an .rb that adds the ext directory to the
        # path, before requiring the .dll (including the extension).
        build_conf = File.join(top_dir, "build.conf")
        File.open(build_conf) do |f|
          f.each do |line|
            if /^\[swig_(.+)\]\s*$/ =~ line
              lib_name = $1
              FileUtils.cp(File.join(build_base_dir, "#{lib_name}.dll"), ext_dir)
              File.open(File.join(ext_dir, "#{lib_name}.rb" ), 'w') do |rb|
                rb.puts(<<-'EOC')
ext_dir = File.expand_path(File.dirname(__FILE__)).tr('/','\\')
unless ENV["PATH"].split(';').include?(ext_dir)
  ENV["PATH"] = "#{ext_dir};#{ENV['PATH']}"
end
require File.join(ext_dir, File.basename(__FILE__, '.rb')) + '.dll'
EOC
              end
            end
          end
        end
      end
    end
  end
end
