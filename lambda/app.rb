# frozen_string_literal: true

require 'stringio'
require 'digest'
require 'json'
require './param_parser'
require './param_serializer'
require './compiler'

module LambdaFunction
  # Handle a non-HTTP compile request, returning a JSON body of either the
  # compiled result or an error.
  class Handler
    include ParamParser

    REVISION = ENV.fetch('REVISION', 'unknown')

    def self.process(event:, context:)
      return { type: 'keep_alive' } if event.has_key?('keep_alive')

      self.new(event).process
    end

    attr_reader :params
    def initialize(event)
      @params = event
    end

    def process
      board             = parse_param('board', with: ParamSerializer::String, default: 'glove80')
      keymap_data       = parse_param('keymap', with: ParamSerializer::Base64)
      snippets          = parse_array_param('snippets', with: ParamSerializer::String, default: [])
      lhs_kconfig_data  = parse_param('kconfig', with: ParamSerializer::Base64, default: nil)
      rhs_kconfig_data  = parse_param('rhs_kconfig', with: ParamSerializer::Base64, default: nil)

      result, log =
        begin
          log_compile(board, keymap_data, lhs_kconfig_data, rhs_kconfig_data, snippets)
          Compiler.new.compile(board, keymap_data, lhs_kconfig_data, rhs_kconfig_data, snippets)
        rescue Compiler::CompileError => e
          return error(status: e.status, message: e.message, detail: e.log, exception: e)
        end

      result = Base64.strict_encode64(result)

      { type: 'result', result: result, log: log, revision: REVISION }
    rescue ParamParser::ParseError => e
      error(status: 400, message: "Error parsing #{e.param}", detail: [e.message], exception: e)
    rescue StandardError => e
      error(status: 500, message: "Unexpected error: #{e.class}", detail: [e.message], exception: e)
    end

    def log_compile(board, keymap_data, kconfig_data, rhs_kconfig_data, snippets)
      keymap      = Digest::SHA1.base64digest(keymap_data)
      kconfig     = kconfig_data ? Digest::SHA1.base64digest(kconfig_data) : 'nil'
      rhs_kconfig = rhs_kconfig_data ? Digest::SHA1.base64digest(rhs_kconfig_data) : 'nil'
      puts("Compiling with board: #{board}; keymap: #{keymap}; kconfig: #{kconfig}; rhs_kconfig: #{rhs_kconfig}; snippets: #{snippets.inspect}")
    end

    def error(status:, message:, detail: nil, exception: nil)
      reported_error = { type: 'error', status:, message:, detail:, revision: REVISION }

      exception_detail = { class: exception.class, backtrace: exception.backtrace } if exception
      logged_error = reported_error.merge(exception: exception_detail)
      puts(JSON.dump(logged_error))

      reported_error
    end
  end
end
