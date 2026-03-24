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

require 'json'
require 'date'
require 'digest'

class ParamSerializer
  class LoadError < ArgumentError; end
  class DumpError < ArgumentError; end

  attr_reader :clazz

  def initialize(clazz)
    @clazz = clazz
  end

  def dump(val, json: false)
    matches_type!(val)
    if json && self.class.json_value?
      val
    else
      val.to_s
    end
  end

  def load(_val)
    raise StandardError.new('unimplemented')
  end

  def matches_type?(val)
    val.is_a?(clazz)
  end

  def matches_type!(val, err: DumpError)
    unless matches_type?(val)
      raise err.new("Incorrect type for #{self.class.name}: #{val.inspect}:#{val.class.name}")
    end
    true
  end

  @registry = {}
  class << self
    def load(...)
      singleton.load(...)
    end

    def dump(...)
      singleton.dump(...)
    end

    def matches_type?(...)
      singleton.matches_type?(...)
    end

    def matches_type!(...)
      singleton.matches_type!(...)
    end

    def singleton
      raise ArgumentError.new("Singleton instance not defined for abstract serializer '#{self.name}'")
    end

    def json_value?
      false
    end

    def for(name)
      @registry[name.to_s]
    end

    def for!(name)
      s = self.for(name)
      raise ArgumentError.new("No serializer registered with name: '#{name}'") if s.nil?
      s
    end

    private

    def set_singleton!
      instance = self.new
      define_singleton_method(:singleton) { instance }
    end

    def json_value!
      define_singleton_method(:json_value?) { true }
    end
  end

  class String < ParamSerializer
    def initialize
      super(::String)
    end

    def load(str)
      matches_type!(str, err: LoadError)
      str
    end

    set_singleton!
    json_value!
  end

  class Integer < ParamSerializer
    def initialize
      super(::Integer)
    end

    # JSON only supports floats, so we have to accept a value
    # which may have already been parsed into a Ruby Float or Integer.
    def load(str_or_num)
      raise LoadError.new("Invalid integer: #{str_or_num}") unless [::String, ::Integer].any? { |t| str_or_num.is_a?(t) }
      Integer(str_or_num)
    rescue ArgumentError => e
      raise LoadError.new(e.message)
    end

    set_singleton!
    json_value!
  end

  class Float < ParamSerializer
    def initialize
      super(::Float)
    end

    def load(str)
      Float(str)
    rescue TypeError, ArgumentError => _e
      raise LoadError.new("Invalid type for conversion to Float")
    end

    set_singleton!
    json_value!
  end

  class Boolean < ParamSerializer
    def initialize
      super(nil)
    end

    def load(str)
      str = str.downcase if str.is_a?(::String)

      if ['false', 'no', 'off', false, '0', 0].include?(str)
        false
      elsif ['true', 'yes', 'on', true, '1', 1].include?(str)
        true
      else
        raise LoadError.new("Invalid boolean: #{str.inspect}")
      end
    end

    def matches_type?(val)
      [true, false].include?(val)
    end

    set_singleton!
    json_value!
  end

  class Numeric < ParamSerializer
    def initialize
      super(::Numeric)
    end

    def load(str)
      Float(str)
    rescue TypeError, ArgumentError => _e
      raise LoadError.new("Invalid type for conversion to Numeric")
    end

    set_singleton!
    json_value!
  end

  # Abstract serializer for ISO8601 dates and times
  class ISO8601 < ParamSerializer
    def load(str)
      raise TypeError.new unless str.is_a?(::String)

      clazz.parse(str)
    rescue TypeError, ArgumentError => _e
      raise LoadError.new("Invalid type for conversion to #{clazz}")
    end

    def dump(val, json: nil)
      matches_type!(val)
      val.iso8601
    end
  end

  class Date < ISO8601
    def initialize
      super(::Date)
    end

    set_singleton!
  end

  class Time < ISO8601
    def initialize
      super(::Time)
    end

    set_singleton!
  end

  class UUID < ParamSerializer::String
    def load(str)
      matches_type!(str, err: LoadError)
      super
    end

    def matches_type?(str)
      super && /[0-9a-f]{8}-([0-9a-f]{4}-){3}[0-9a-f]{12}/i.match?(str)
    end

    set_singleton!
    json_value!
  end

  # Abstract serializer for members of a fixed set of lowercase strings,
  # case-normalized on parse.
  class StringEnum < ParamSerializer
    def initialize(*members)
      @member_set = members.map { |s| normalize(s) }.to_set.freeze
      super(nil)
    end

    def load(str)
      val = normalize(str.to_s)
      matches_type!(val, err: LoadError)
      val
    end

    def matches_type?(str)
      str.is_a?(::String) && @member_set.include?(str)
    end

    def normalize(str)
      str.downcase
    end
  end

  class CaseSensitiveStringEnum < StringEnum
    def normalize(str)
      str
    end
  end

  class Base64 < ParamSerializer
    def initialize
      super(::String)
    end

    def load(base64)
      ::Base64.strict_decode64(base64)
    rescue ArgumentError => _e
      raise LoadError.new('Invalid Base64')
    end

    def dump(str, json: nil)
      matches_type!(str)
      ::Base64.strict_encode64(str)
    end

    set_singleton!
    json_value!
  end
end
