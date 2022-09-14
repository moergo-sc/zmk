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
        unless event['path'] == '/compile' || event['path'] == '/api/compile'
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
        in_build_dir do
          File.open('build.keymap', 'w') do |io|
            io.write(keymap_data)
          end

          compile_output = nil

          IO.popen(['compileZmk', './build.keymap'], err: [:child, :out]) do |io|
            compile_output = io.read
          end

          compile_output = compile_output.split("\n")

          unless $?.success?
            status = $?.exitstatus
            return error_response(400, error: "Compile failed with exit status #{status}", detail: compile_output)
          end

          unless File.exist?('zephyr/combined.uf2')
            return error_response(500, error: 'Compile failed to produce result binary', detail: compile_output)
          end

          file_response(File.read('zephyr/combined.uf2'), compile_output)
        rescue StandardError => e
          error_response(500, error: 'Unexpected error', detail: e.message)
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

      def file_response(file, compile_output)
        file64 = Base64.strict_encode64(file)

        headers = {
          'Content-Type' => 'application/octet-stream',
        }

        headers.merge!('X-Debug-Output' => compile_output.to_json) if ENV.include?('DEBUG')

        {
          'isBase64Encoded' => true,
          'statusCode' => 200,
          'body' => file64,
          'headers' => headers,
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
