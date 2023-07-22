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

  def compile(keymap_data, lhs_kconfig_data, rhs_kconfig_data)
    if rhs_kconfig_data && !rhs_kconfig_data.empty?
      lhs_result, lhs_output = compile_board('glove80_lh', keymap_data:,     kconfig_data: lhs_kconfig_data, include_static_rhs: false)
      rhs_result, rhs_output = compile_board('glove80_rh', keymap_data: nil, kconfig_data: rhs_kconfig_data, include_static_rhs: false)
      [
        lhs_result.concat(rhs_result),
        ["LHS Output:", *lhs_output, "RHS Output:", *rhs_output],
      ]
    else
      compile_board('glove80_lh', keymap_data:, kconfig_data: lhs_kconfig_data, include_static_rhs: true)
    end
  end

  def compile_board(board, keymap_data:, kconfig_data:, include_static_rhs: false)
    in_build_dir do
      compile_command = ['compileZmk', '-b', board]

      if keymap_data
        File.open('build.keymap', 'w') { |io| io.write(keymap_data) }
        compile_command << '-k' << './build.keymap'
      end

      if kconfig_data
        File.open('build.conf', 'w') { |io| io.write(kconfig_data) }
        compile_command << '-c' << './build.conf'
      end

      if include_static_rhs
        # Concatenate the pre-compiled glove80_rh image to the resulting uf2
        compile_command << '-m'
      end

      compile_output = nil

      IO.popen(compile_command, 'rb', err: [:child, :out]) do |io|
        compile_output = io.read
      end

      compile_output = compile_output.split("\n")

      unless $?.success?
        status = $?.exitstatus
        raise CompileError.new("Compile failed with exit status #{status}", log: compile_output)
      end

      unless File.exist?('zmk.uf2')
        raise CompileError.new('Compile failed to produce result binary', status: 500, log: compile_output)
      end

      result = File.read('zmk.uf2')

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
