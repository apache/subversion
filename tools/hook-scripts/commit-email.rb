#!/usr/bin/env ruby

require "optparse"
require "ostruct"
require "stringio"
require "tempfile"

SENDMAIL = "/usr/sbin/sendmail"

def parse(args)
  options = OpenStruct.new
  options.to = []
  options.error_to = []
  options.from = nil
  options.add_diff = true
  options.repository_uri = nil
  options.rss_path = nil
  options.rss_uri = nil
  options.name = nil

  opts = OptionParser.new do |opts|
    opts.separator ""

    opts.on("-I", "--include [PATH]",
            "Add [PATH] to load path") do |path|
      $LOAD_PATH.unshift(path)
    end
    
    opts.on("-t", "--to [TO]",
            "Add [TO] to to address") do |to|
      options.to << to unless to.nil?
    end
    
    opts.on("-e", "--error-to [TO]",
            "Add [TO] to to address when error is occurred") do |to|
      options.error_to << to unless to.nil?
    end
    
    opts.on("-f", "--from [FROM]",
            "Use [FROM] as from address") do |from|
      options.from = from
    end
    
    opts.on("-n", "--no-diff",
            "Don't add diffs") do |from|
      options.add_diff = false
    end
    
    opts.on("-r", "--repository-uri [URI]",
            "Use [URI] as URI of repository") do |uri|
      options.repository_uri = uri
    end
    
    opts.on("--rss-path [PATH]",
            "Use [PATH] as output RSS path") do |path|
      options.rss_path = path
    end
    
    opts.on("--rss-uri [URI]",
            "Use [URI] as output RSS URI") do |uri|
      options.rss_uri = uri
    end
    
    opts.on("--name [NAME]",
            "Use [NAME] as repository name") do |name|
      options.name = name
    end
    
    opts.on_tail("--help", "Show this message") do
      puts opts
      exit!
    end
  end

  opts.parse!(args)

  options
end

def make_body(info, params)
  body = ""
  body << "#{info.author}\t#{format_time(info.date)}\n"
  body << "\n"
  body << "  New Revision: #{info.revision}\n"
  body << "\n"
  body << added_dirs(info)
  body << added_files(info)
  body << copied_dirs(info)
  body << copied_files(info)
  body << deleted_dirs(info)
  body << deleted_files(info)
  body << modified_dirs(info)
  body << modified_files(info)
  body << "\n"
  body << "  Log:\n"
  info.log.each_line do |line|
    body << "    #{line}"
  end
  body << "\n"
  body << change_info(info, params[:repository_uri], params[:add_diff])
  body
end

def format_time(time)
  time.strftime('%Y-%m-%d %X %z (%a, %d %b %Y)')
end

def changed_items(title, type, items)
  rv = ""
  unless items.empty?
    rv << "  #{title} #{type}:\n"
    if block_given?
      yield(rv, items)
    else
      rv << items.collect {|item| "    #{item}\n"}.join('')
    end
  end
  rv
end

def changed_files(title, files, &block)
  changed_items(title, "files", files, &block)
end

def added_files(info)
  changed_files("Added", info.added_files)
end

def deleted_files(info)
  changed_files("Removed", info.deleted_files)
end

def modified_files(info)
  changed_files("Modified", info.updated_files)
end

def copied_files(info)
  changed_files("Copied", info.copied_files) do |rv, files|
    rv << files.collect do |file, from_file, from_rev|
      <<-INFO
    #{file}
      (from rev #{from_rev}, #{from_file})
INFO
    end.join("")
  end
end

def changed_dirs(title, files, &block)
  changed_items(title, "directories", files, &block)
end

def added_dirs(info)
  changed_dirs("Added", info.added_dirs)
end

def deleted_dirs(info)
  changed_dirs("Removed", info.deleted_dirs)
end

def modified_dirs(info)
  changed_dirs("Modified", info.updated_dirs)
end

def copied_dirs(info)
  changed_dirs("Copied", info.copied_dirs) do |rv, dirs|
    rv << dirs.collect do |dir, from_dir, from_rev|
      "    #{dir} (from rev #{from_rev}, #{from_dir})\n"
    end.join("")
  end
end


CHANGED_TYPE = {
  :added => "Added",
  :modified => "Modified",
  :deleted => "Deleted",
  :copied => "Copied",
  :property_changed => "Property changed",
}

CHANGED_MARK = Hash.new("=")
CHANGED_MARK[:property_changed] = "_"

def change_info(info, uri, add_diff)
  result = changed_dirs_info(info, uri)
  result = "\n#{result}" unless result.empty?
  result << "\n"
  diff_info(info, uri, add_diff).each do |key, infos|
    infos.each do |desc, link|
      result << "#{desc}\n"
    end
  end
  result
end

def changed_dirs_info(info, uri)
  rev = info.revision
  (info.added_dirs.collect do |dir|
     "  Added: #{dir}\n"
   end + info.copied_dirs.collect do |dir, from_dir, from_rev|
     <<-INFO
  Copied: #{dir}
    (from rev #{from_rev}, #{from_dir})
INFO
   end + info.deleted_dirs.collect do |dir|
     <<-INFO
  Deleted: #{dir}
    % svn ls #{[uri, dir].compact.join("/")}@#{rev - 1}
INFO
   end + info.updated_dirs.collect do |dir|
     "  Modified: #{dir}\n"
   end).join("\n")
end

def diff_info(info, uri, add_diff)
  info.diffs.collect do |key, values|
    [
      key,
      values.collect do |type, value|
        args = []
        rev = info.revision
        case type
        when :added
          command = "cat"
        when :modified, :property_changed
          command = "diff"
          args.concat(["-r", "#{info.revision - 1}:#{info.revision}"])
        when :deleted
          command = "cat"
          rev -= 1
        when :copied
          command = "cat"
        else
          raise "unknown diff type: #{value.type}"
        end

        command += " #{args.join(' ')}" unless args.empty?

        link = [uri, key].compact.join("/")

        line_info = "+#{value.added_line} -#{value.deleted_line}"
        desc = <<-HEADER
  #{CHANGED_TYPE[value.type]}: #{key} (#{line_info})
#{CHANGED_MARK[value.type] * 67}
HEADER

        if add_diff
          desc << value.body
        else
          desc << <<-CONTENT
    % svn #{command} #{link}@#{rev}
CONTENT
        end
      
        [desc, link]
      end
    ]
  end
end

def make_header(to, from, info, params)
  headers = []
  headers << x_author(info)
  headers << x_repository(info)
  headers << x_id(info)
  headers << x_sha256(info)
  headers << "Content-Type: text/plain; charset=UTF-8"
  headers << "Content-Transfer-Encoding: 8bit"
  headers << "From: #{from}"
  headers << "To: #{to.join(' ')}"
  headers << "Subject: #{make_subject(params[:name], info)}"
  headers.find_all do |header|
    /\A\s*\z/ !~ header
  end.join("\n")
end

def make_subject(name, info)
  subject = ""
  subject << "#{name}:" if name
  subject << "r#{info.revision}: "
  subject << info.log.lstrip.to_a.first.to_s.chomp
  NKF.nkf("-WM", subject)
end

def x_author(info)
  "X-SVN-Author: #{info.author}"
end

def x_repository(info)
  # "X-SVN-Repository: #{info.path}"
  "X-SVN-Repository: XXX"
end

def x_id(info)
  "X-SVN-Commit-Id: #{info.entire_sha256}"
end

def x_sha256(info)
  info.sha256.collect do |name, inf|
    "X-SVN-SHA256-Info: #{name}, #{inf[:revision]}, #{inf[:sha256]}"
  end.join("\n")
end

def make_mail(to, from, info, params)
  make_header(to, from, info, params) + "\n" + make_body(info, params)
end

def sendmail(to, from, mail)
  args = to.collect {|address| address.dump}.join(' ')
  open("| #{SENDMAIL} #{args}", "w") do |f|
    f.print(mail)
  end
end

def output_rss(name, file, rss_uri, repos_uri, info)
  prev_rss = nil
  begin
    if File.exist?(file)
      File.open(file) do |f|
        prev_rss = RSS::Parser.parse(f)
      end
    end
  rescue RSS::Error
  end

  File.open(file, "w") do |f|
    f.print(make_rss(prev_rss, name, rss_uri, repos_uri, info).to_s)
  end
end

def make_rss(base_rss, name, rss_uri, repos_uri, info)
  RSS::Maker.make("1.0") do |maker|
    maker.encoding = "UTF-8"

    maker.channel.about = rss_uri
    maker.channel.title = rss_title(name || repos_uri)
    maker.channel.link = repos_uri
    maker.channel.description = rss_title(name || repos_uri)
    maker.channel.dc_date = info.date

    if base_rss
      base_rss.items.each do |item|
        item.setup_maker(maker) 
      end
    end
    
    diff_info(info, repos_uri, true).each do |name, infos|
      infos.each do |desc, link|
        item = maker.items.new_item
        item.title = name
        item.description = info.log
        item.content_encoded = "<pre>#{h(desc)}</pre>"
        item.link = link
        item.dc_date = info.date
        item.dc_creator = info.author
      end
    end

    maker.items.do_sort = true
    maker.items.max_size = 15
  end
end

def rss_title(name)
  "Repository of #{name}"
end

def rss_items(items, info, repos_uri)
  diff_info(info, repos_uri).each do |name, infos|
    infos.each do |desc, link|
      items << [link, name, desc, info.date]
    end
  end
  
  items.sort_by do |uri, title, desc, date|
    date
  end.reverse
end

def main
  if ARGV.find {|arg| arg == "--help"}
    parse(ARGV)
  else
    repos, revision, to, *rest = ARGV
    options = parse(rest)
  end
  
  require "svn/info"
  info = Svn::Info.new(repos, revision)
  from = options.from || info.author
  to = [to, *options.to]
  params = {
    :repository_uri => options.repository_uri,
    :name => options.name,
    :add_diff => options.add_diff,
  }
  sendmail(to, from, make_mail(to, from, info, params))

  if options.repository_uri and
      options.rss_path and
      options.rss_uri
    require "rss/1.0"
    require "rss/dublincore"
    require "rss/content"
    require "rss/maker"
    include RSS::Utils
    output_rss(options.name,
               options.rss_path,
               options.rss_uri,
               options.repository_uri,
               info)
  end
end

begin
  main
rescue Exception
  _, _, to, *rest = ARGV
  to = [to]
  from = ENV["USER"]
  begin
    options = parse(rest)
    to = options.error_to unless options.error_to.empty?
    from = options.from
  rescue Exception
  end
  sendmail(to, from, <<-MAIL)
From: #{from}
To: #{to.join(', ')}
Subject: Error

#{$!.class}: #{$!.message}
#{$@.join("\n")}
MAIL
end
