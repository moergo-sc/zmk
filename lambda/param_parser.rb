# frozen_string_literal: true

# Copyright 2016 DMM.com LLC

# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the “Software”), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:

# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.

# THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

require './param_serializer'

module ParamParser
  class ParseError < RuntimeError
    attr_accessor :param, :value

    def initialize(message, param, value)
      super(message)
      self.param = param
      self.value = value
    end
  end

  PARAM_REQUIRED = Object.new
  BLANK = Object.new

  # Parse the specified parameter, optionally deserializing with the provided
  # ParamSerializer. If the parameter is missing and no default is
  # provided, raises a ParseError.
  #
  # If `BLANK` is provided as a default, return a placeholder object that can be
  # later stripped out with `remove_blanks`
  #
  # If `dump` is true, use the serializer to re-serialize any successfully
  # parsed argument back to a canonical string. This can be useful to validate
  # and normalize the input to another service without parsing it. A serializer
  # must be passed to use this option.
  def parse_param(param, with: nil, default: PARAM_REQUIRED, dump: false)
    serializer =
      case with
      when String, Symbol
        ParamSerializer.for!(with)
      else
        with
      end

    parse =
      if !params.has_key?(param)
        raise ParseError.new("Required parameter '#{param}' missing", param, nil) if default == PARAM_REQUIRED
        default
      else
        val = params[param]
        if !serializer.nil?
          begin
            serializer.load(val)
          rescue ParamSerializer::LoadError => ex
            raise ParseError.new("Invalid parameter '#{param}': '#{val.inspect}' - #{ex.message}", param, val)
          end
        else
          val
        end
      end

    if dump && parse != BLANK
      begin
        parse = serializer.dump(parse)
      rescue NoMethodError => ex
        raise ParseError.new("Serializer '#{serializer}' can't dump param '#{param}' #{val.inspect} - #{ex.message}", param, val)
      end
    end

    parse
  end

  # Parse an array-typed param using the provided serializer for each member element.
  def parse_array_param(param, with: nil, default: PARAM_REQUIRED, dump: false)
    serializer =
      case with
      when String, Symbol
        ParamSerializer.for!(with)
      else
        with
      end

    vals = params[param]

    parses =
      if vals.nil?
        raise ParseError.new("Required parameter '#{param}' missing", param, nil) if default == PARAM_REQUIRED
        default
      elsif !vals.is_a?(Array)
        raise ParseError.new("Invalid type for parameter '#{param}': '#{vals.class.name}'", param, vals)
      elsif !serializer.nil?
        vals.map do |val|
          begin
            serializer.load(val)
          rescue ParamSerializer::LoadError => ex
            raise ParseError.new("Invalid member in array parameter '#{param}': '#{val.inspect}' - #{ex.message}", param, val)
          end
        end
      else
        vals
      end

    if dump && parses != BLANK
      parses.map! { |v| serializer.dump(v) }
    end

    parses
  end

  # Convenience method to make it simpler to build a hash structure with
  # optional members from parsed data. This method recursively traverses the
  # provided structure and removes any instances of the sentinel value
  # Parser::BLANK.
  def remove_blanks(arg)
    case arg
    when Hash
      arg.each do |k, v|
        if v == BLANK
          arg.delete(k)
        else
          remove_blanks(v)
        end
      end
    when Array
      arg.delete(BLANK)
      arg.each { |e| remove_blanks(e) }
    end
  end
end
