# frozen_string_literal: true

require 'tmpdir'
require 'json'
require 'base64'

module LambdaFunction
  class Handler
    class << self
      # ALB event structure:
      # {
      #   "requestContext": { <snip> },
      #   "httpMethod": "GET",
      #   "path": "/",
      #   "queryStringParameters": {parameters},
      #   "headers": { <snip> },
      #   "isBase64Encoded": false,
      #   "body": "request_body"
      # }
      #
      # Handle the single route: POST /compile
      def process(event:, context:)
        unless event['path'] == '/compile'
          return error_response(404, error: "Unknown route: #{event['path']}")
        end

        unless event['httpMethod'] == 'POST'
          return error_response(404, error: "No route for HTTP method : #{event['httpMethod']}")
        end

        keymap_data = event['body']

        unless keymap_data
          return error_response(400, error: 'Missing POST body')
        end

        if event['isBase64Encoded']
          keymap_data = Base64.decode64(keymap_data)
        end

        compile(keymap_data)
      end

      private

      def compile(keymap_data)
        Dir.mktmpdir do |dir|
          Dir.chdir(dir)

          File.open('build.keymap', 'w') do |io|
            io.write(keymap_data)
          end

          compile_output = nil
          IO.popen(['compileZmk', './build.keymap'], err: [:child, :out]) do |io|
            compile_output = io.read
          end

          unless $?.success?
            status = $?.exitstatus
            return error_response(400, error: "Compile failed with exit status #{status}", detail: compile_output)
          end

          unless File.exist?('zephyr/zmk.uf2')
            return error_response(500, error: 'Compile failed to produce result binary')
          end

          file_response(File.read('zephyr/zmk.uf2'))
        end
      rescue StandardError => e
        error_response(500, error: 'Unexpected error', detail: e.message)
      end

      def file_response(file)
        file64 = Base64.strict_encode64(file)

        {
          'isBase64Encoded' => true,
          'statusCode' => 200,
          'body' => file64,
          'headers' => {
            'Content-Type' => 'application/octet-stream'
          }
        }
      end

      def error_response(code, error:, detail: nil)
        {
          'isBase64Encoded' => false,
          'statusCode' => code,
          'body' => { error: error, detail: detail }.to_json,
          'headers' => {
            'Content-Type' => 'application/json'
          }
        }
      end
    end
  end
end
