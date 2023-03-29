# frozen_string_literal: true

require 'tmpdir'
require 'json'
require 'base64'

class Compiler
  class CompileError < RuntimeError
    attr_reader :status, :log

    def initialize(message, status: 400, log:)
      super(message)
      @status = status
      @log = log
    end
  end

  def compile(keymap_data, kconfig_data)
    in_build_dir do
      compile_command = ['compileZmk', './build.keymap']

      File.open('build.keymap', 'w') { |io| io.write(keymap_data) }

      if kconfig_data
        File.open('build.conf', 'w') { |io| io.write(kconfig_data) }
        compile_command << './build.conf'
      end

      compile_output = nil

      IO.popen(compile_command, err: [:child, :out]) do |io|
        compile_output = io.read
      end

      compile_output = compile_output.split("\n")

      unless $?.success?
        status = $?.exitstatus
        raise CompileError.new("Compile failed with exit status #{status}", log: compile_output)
      end

      unless File.exist?('zephyr/combined.uf2')
        raise CompileError.new('Compile failed to produce result binary', status: 500, log: compile_output)
      end

      result = File.read('zephyr/combined.uf2')

      [result, compile_output]
    end
  end

  # Lambda is single-process per container, and we get substantial speedups
  # from ccache by always building in the same path
  BUILD_DIR = '/tmp/build'

  def in_build_dir
    FileUtils.remove_entry(BUILD_DIR, true)
    Dir.mkdir(BUILD_DIR)
    Dir.chdir(BUILD_DIR)
    yield
  ensure
    FileUtils.remove_entry(BUILD_DIR, true) rescue nil
  end
end
