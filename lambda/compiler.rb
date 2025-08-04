# frozen_string_literal: true

require 'tmpdir'
require 'base64'
require 'json'
require 'open3'
require 'yaml'
require 'set'

class Compiler
  class CompileError < RuntimeError
    attr_reader :status, :log

    def initialize(message, status: 400, log:)
      super(message)
      @status = status
      @log = log
    end
  end

  def compile(board, keymap_data, lhs_kconfig_data, rhs_kconfig_data)
    board_lh = board + '_lh'
    board_rh = board + '_rh'

    if rhs_kconfig_data && !rhs_kconfig_data.empty?
      lhs_result, lhs_output = compile_board(board_lh, keymap_data:, kconfig_data: lhs_kconfig_data, include_static_rhs: false)
      rhs_result, rhs_output = compile_board(board_rh, keymap_data:, kconfig_data: rhs_kconfig_data, include_static_rhs: false)
      [
        lhs_result.concat(rhs_result),
        ["LHS Output:", *lhs_output, "RHS Output:", *rhs_output],
      ]
    else
      compile_board(board_lh, keymap_data:, kconfig_data: lhs_kconfig_data, include_static_rhs: true)
    end
  end

  def compile_board(board, keymap_data:, kconfig_data:, include_static_rhs: false)
    in_build_dir do
      compile_command = ['compileZmk', '-b', board]

      if keymap_data
        dts_parse_errors = validate_devicetree!(keymap_data)
        File.open('build.keymap', 'w') { |io| io.write(keymap_data) }
        compile_command << '-k' << './build.keymap'
      end

      if kconfig_data
        File.open('build.conf', 'w') { |io| io.write(kconfig_data) }
        compile_command << '-c' << './build.conf'

        # If requesting USB logging, include the corresponding snippet that also
        # enables necessary devicetree nodes
        if kconfig_data =~ /CONFIG_ZMK_USB_LOGGING\s*=\s*y/
          compile_command << '-s' << 'zmk-usb-logging'
        end
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

      if dts_parse_errors
        # DTS validation failed to parse the DTS, yet the Zephyr build
        # nonetheless succeeded. We can't allow returning the result, since we
        # were unable to check it for unsafe dts sections.
        raise CompileError.new('Syntax error validating device-tree input', log: dts_parse_errors)
      end

      result = File.read('zmk.uf2')

      [result, compile_output]
    end
  end

  PERMITTED_DTS_SECTIONS = %w[
    behaviors macros combos conditional_layers keymap underglow-indicators underglow-layer
    cirque_lh_listener cirque_rh_listener
    input_processors
    mkp_input_listener mmv_input_listener msc_input_listener
  ].to_set.freeze

  PERMITTED_DTS_SECTION_PATTERNS = [
    /^zip_/
  ].freeze

  DTS_STUB = <<~EOF
    /dts-v1/;
    / {
      underglow_indicators: underglow-indicators {};
      cirque_lh_listener: cirque_lh_listener {};
      cirque_rh_listener: cirque_rh_listener {};
    };
  EOF

  def validate_devicetree!(dtsi)
    dts = DTS_STUB + dtsi

    stdout, stderr, status =
      Open3.capture3({}, 'dts2yml', unsetenv_others: true, stdin_data: dts)

    unless status.success?
      # The error output from dtc is much harder to understand than Zephyr's
      # errors, and the line numbers don't match up due to preprocessing. Rather
      # than raising these now, return the error output in order that it's only
      # used in the case that the Zephyr build doesn't itself error.
      return stderr.split("\n")
    end

    data =
      begin
        YAML.safe_load(stdout)
      rescue Psych::Exception => e
        raise CompileError.new('Error parsing translated device-tree', status: 500, log: [e.message])
      end

    sections = data.flat_map(&:keys)

    invalid_sections = sections.reject do |section_name|
      PERMITTED_DTS_SECTIONS.include?(section_name) ||
        PERMITTED_DTS_SECTION_PATTERNS.any? { |p| p.match?(section_name) }
    end

    unless invalid_sections.empty?
      raise CompileError.new(
              "Device-tree included the non-permitted root sections: #{invalid_sections.inspect}", log: [])
    end

    nil
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
