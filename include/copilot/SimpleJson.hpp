#pragma once

#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace copilot::json {

class Value {
public:
    using Object = std::map<std::string, Value>;

    Value() : data_(nullptr) {}
    Value(std::nullptr_t) : data_(nullptr) {}
    Value(bool v) : data_(v) {}
    Value(int v) : data_(static_cast<double>(v)) {}
    Value(unsigned int v) : data_(static_cast<double>(v)) {}
    Value(long long v) : data_(static_cast<double>(v)) {}
    Value(unsigned long long v) : data_(static_cast<double>(v)) {}
    Value(double v) : data_(v) {}
    Value(const char* v) : data_(std::string(v)) {}
    Value(std::string v) : data_(std::move(v)) {}
    Value(Object v) : data_(std::move(v)) {}

    bool isObject() const { return std::holds_alternative<Object>(data_); }
    bool isString() const { return std::holds_alternative<std::string>(data_); }
    bool isNumber() const { return std::holds_alternative<double>(data_); }
    bool isBool() const { return std::holds_alternative<bool>(data_); }

    const Object* object() const { return std::get_if<Object>(&data_); }
    Object* object() { return std::get_if<Object>(&data_); }

    const Value* find(std::string_view key) const {
        const auto* obj = object();
        if (!obj) return nullptr;
        const auto it = obj->find(std::string(key));
        return it == obj->end() ? nullptr : &it->second;
    }

    std::string stringOr(std::string fallback = {}) const {
        if (const auto* value = std::get_if<std::string>(&data_)) return *value;
        return fallback;
    }

    double numberOr(double fallback = 0.0) const {
        if (const auto* value = std::get_if<double>(&data_)) return *value;
        return fallback;
    }

    bool boolOr(bool fallback = false) const {
        if (const auto* value = std::get_if<bool>(&data_)) return *value;
        return fallback;
    }

    std::string dump() const {
        std::ostringstream out;
        dumpTo(out);
        return out.str();
    }

    static std::optional<Value> parse(std::string_view input) {
        Parser parser(input);
        auto value = parser.parseValue();
        parser.skipWhitespace();
        if (!value || !parser.atEnd()) return std::nullopt;
        return value;
    }

private:
    class Parser {
    public:
        explicit Parser(std::string_view input) : input_(input) {}

        void skipWhitespace() {
            while (pos_ < input_.size() &&
                   std::isspace(static_cast<unsigned char>(input_[pos_]))) {
                ++pos_;
            }
        }

        bool atEnd() const { return pos_ == input_.size(); }

        std::optional<Value> parseValue() {
            skipWhitespace();
            if (pos_ >= input_.size()) return std::nullopt;

            const char c = input_[pos_];
            if (c == '{') return parseObject();
            if (c == '"') {
                auto s = parseString();
                if (!s) return std::nullopt;
                return Value(std::move(*s));
            }
            if (c == 't' && consumeLiteral("true")) return Value(true);
            if (c == 'f' && consumeLiteral("false")) return Value(false);
            if (c == 'n' && consumeLiteral("null")) return Value(nullptr);
            if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return parseNumber();
            return std::nullopt;
        }

    private:
        std::optional<Value> parseObject() {
            if (!consume('{')) return std::nullopt;
            Object obj;
            skipWhitespace();
            if (consume('}')) return Value(std::move(obj));

            for (;;) {
                skipWhitespace();
                auto key = parseString();
                if (!key) return std::nullopt;
                skipWhitespace();
                if (!consume(':')) return std::nullopt;
                auto value = parseValue();
                if (!value) return std::nullopt;
                obj.emplace(std::move(*key), std::move(*value));
                skipWhitespace();
                if (consume('}')) break;
                if (!consume(',')) return std::nullopt;
            }
            return Value(std::move(obj));
        }

        std::optional<std::string> parseString() {
            if (!consume('"')) return std::nullopt;
            std::string result;
            while (pos_ < input_.size()) {
                const char c = input_[pos_++];
                if (c == '"') return result;
                if (static_cast<unsigned char>(c) < 0x20) return std::nullopt;
                if (c != '\\') {
                    result.push_back(c);
                    continue;
                }
                if (pos_ >= input_.size()) return std::nullopt;
                const char escaped = input_[pos_++];
                switch (escaped) {
                    case '"': result.push_back('"'); break;
                    case '\\': result.push_back('\\'); break;
                    case '/': result.push_back('/'); break;
                    case 'b': result.push_back('\b'); break;
                    case 'f': result.push_back('\f'); break;
                    case 'n': result.push_back('\n'); break;
                    case 'r': result.push_back('\r'); break;
                    case 't': result.push_back('\t'); break;
                    default: return std::nullopt;
                }
            }
            return std::nullopt;
        }

        std::optional<Value> parseNumber() {
            const std::size_t start = pos_;
            if (input_[pos_] == '-') ++pos_;
            if (pos_ >= input_.size()) return std::nullopt;
            if (input_[pos_] == '0') {
                ++pos_;
            } else {
                if (!std::isdigit(static_cast<unsigned char>(input_[pos_]))) return std::nullopt;
                while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) ++pos_;
            }
            if (pos_ < input_.size() && input_[pos_] == '.') {
                ++pos_;
                if (pos_ >= input_.size() || !std::isdigit(static_cast<unsigned char>(input_[pos_]))) return std::nullopt;
                while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) ++pos_;
            }
            if (pos_ < input_.size() && (input_[pos_] == 'e' || input_[pos_] == 'E')) {
                ++pos_;
                if (pos_ < input_.size() && (input_[pos_] == '+' || input_[pos_] == '-')) ++pos_;
                if (pos_ >= input_.size() || !std::isdigit(static_cast<unsigned char>(input_[pos_]))) return std::nullopt;
                while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) ++pos_;
            }
            const std::string token(input_.substr(start, pos_ - start));
            char* end = nullptr;
            const double value = std::strtod(token.c_str(), &end);
            if (!end || *end != '\0') return std::nullopt;
            return Value(value);
        }

        bool consume(char expected) {
            if (pos_ < input_.size() && input_[pos_] == expected) {
                ++pos_;
                return true;
            }
            return false;
        }

        bool consumeLiteral(std::string_view literal) {
            if (input_.substr(pos_, literal.size()) == literal) {
                pos_ += literal.size();
                return true;
            }
            return false;
        }

        std::string_view input_;
        std::size_t pos_{};
    };

    static void dumpString(std::ostringstream& out, const std::string& value) {
        out << '"';
        for (unsigned char c : value) {
            switch (c) {
                case '"': out << "\\\""; break;
                case '\\': out << "\\\\"; break;
                case '\b': out << "\\b"; break;
                case '\f': out << "\\f"; break;
                case '\n': out << "\\n"; break;
                case '\r': out << "\\r"; break;
                case '\t': out << "\\t"; break;
                default:
                    if (c < 0x20) {
                        out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                            << static_cast<int>(c) << std::dec << std::setfill(' ');
                    } else {
                        out << static_cast<char>(c);
                    }
            }
        }
        out << '"';
    }

    void dumpTo(std::ostringstream& out) const {
        if (std::holds_alternative<std::nullptr_t>(data_)) {
            out << "null";
        } else if (const auto* value = std::get_if<bool>(&data_)) {
            out << (*value ? "true" : "false");
        } else if (const auto* value = std::get_if<double>(&data_)) {
            out << std::setprecision(15) << *value;
        } else if (const auto* value = std::get_if<std::string>(&data_)) {
            dumpString(out, *value);
        } else if (const auto* obj = std::get_if<Object>(&data_)) {
            out << '{';
            bool first = true;
            for (const auto& [key, value] : *obj) {
                if (!first) out << ',';
                first = false;
                dumpString(out, key);
                out << ':';
                value.dumpTo(out);
            }
            out << '}';
        }
    }

    std::variant<std::nullptr_t, bool, double, std::string, Object> data_;
};

inline Value object(Value::Object obj) {
    return Value(std::move(obj));
}

} // namespace copilot::json
