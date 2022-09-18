# frozen_string_literal: true

require 'rack'
require 'serverless_rack'

require './web_app'
require './compiler'

$app = Rack::Builder.new do
  run WebApp
end.to_app

module LambdaFunction
  # Handle a API Gateway/ALB-structured HTTP request using the Sinatra app
  class HttpHandler
    def self.process(event:, context:)
      handle_request(app: $app, event: event, context: context)
    end
  end

  # Handle a non-HTTP proxied request, returning either the compiled result or
  # an error as JSON.
  class DirectHandler
    def self.process(event:, context:)
      base64 = event.fetch('base64', false)

      keymap_data = event.fetch('keymap') do
        raise ArgumentError.new('Missing required argument: keymap')
      end

      keymap_data = Base64.decode64(keymap_data) if base64
      result, log = Compiler.new.compile(keymap_data)
      result = Base64.strict_encode64(result) if base64

      { type: 'result', result: result, log: log, base64: base64 }
    rescue Compiler::CompileError => e
      {
        type: 'error',
        status: e.status,
        message: e.message,
        detail: e.detail,
      }
    rescue StandardError => e
      {
        type: 'error',
        status: 500,
        message: "Unexpected error: #{e.class}",
        detail: e.message,
      }
    end
  end
end
