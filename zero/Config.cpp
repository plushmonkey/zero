#include "Config.h"

#include <zero/Utility.h>
#include <zero/game/Logger.h>

#include <string>
#include <string_view>

namespace zero {

static std::string LoadEntireFile(const char* filename) {
  std::string result;

  FILE* f = fopen(filename, "rb");

  if (!f) return result;

  fseek(f, 0, SEEK_END);
  long file_size = ftell(f);
  fseek(f, 0, SEEK_SET);

  result.resize(file_size);
  size_t read_amount = fread(&result[0], 1, file_size, f);
  if (read_amount != file_size) {
    Log(LogLevel::Warning, "Failed to read entire config file.");
  }
  fclose(f);

  return result;
}

namespace ini {

enum class TokenType { BracketOpen, BracketClose, String, Equals, Eof };

inline const char* to_string(TokenType type) {
  static const char* kTokenNames[] = {
      "BracketOpen", "BracketClose", "String", "Equals", "Eof",
  };

  return kTokenNames[(size_t)type];
}

struct Token {
  TokenType type;
  std::string_view data;
  size_t line;

  Token() : type(TokenType::Eof), line(0) {}
  Token(TokenType type, std::string_view data, size_t line) : type(type), data(data), line(line) {}
};

struct Lexer {
  std::string_view data;
  size_t index;

  size_t line;

  Lexer(std::string_view data) : data(data), index(0), line(0) {}

  Token GetNextToken(bool expect_string = false, bool string_to_line_end = false) {
    if (index >= data.size()) return Token();

    bool reading_string = false;
    size_t string_start = 0;

    if (expect_string) {
      while (index < data.size() && IsWhitespace(data[index])) ++index;

      reading_string = true;
      string_start = index;
    }

    while (index < data.size()) {
      size_t current_index = index;
      char c = data[index++];

      // If reading a string and a new token is found, then return the current
      // string
      if (reading_string && (IsWhitespace(c) || IsSpecial(c)) && current_index - string_start > 0) {
        if (c == '\r' || c == '\n' || !string_to_line_end) {
          // Reset the index to the last read token since it isn't part of this
          // string.
          index = current_index;
          return Token(TokenType::String, data.substr(string_start, index - string_start), line);
        }
      }

      if (c == '#' && (!reading_string || current_index - string_start > 0)) {
        while (c != '\n' && c != 0) {
          c = data[index++];
        }
        ++line;
        continue;
      }

      if (IsWhitespace(c)) {
        if (c == '\n') ++line;

        continue;
      }

      switch (c) {
        case '[':
          return Token(TokenType::BracketOpen, data.substr(current_index, 1), line);
        case ']':
          return Token(TokenType::BracketClose, data.substr(current_index, 1), line);
        case '=': {
          return Token(TokenType::Equals, data.substr(current_index, 1), line);
        } break;
        default: {
          if (!reading_string) {
            // Begin reading an entire string until whitespace or special
            // character
            reading_string = true;
            string_start = current_index;
          }
        } break;
      }
    }

    // Handle string if it's the last thing in the file
    if (reading_string) {
      return Token(TokenType::String, data.substr(string_start, index - string_start), line);
    }

    return Token();
  }

  inline bool IsSpecial(char c) {
    if (c == '[') return true;
    if (c == ']') return true;
    if (c == '=') return true;
    if (c == '#') return true;
    return false;
  }

  inline bool IsWhitespace(char c) {
    if (c == ' ') return true;
    if (c == '\n') return true;
    if (c == '\r') return true;
    if (c == '\t') return true;
    return false;
  }
};

static void EmitTokenError(Token token, TokenType expected) {
  const char* recv_name = to_string(token.type);
  const char* expected_name = to_string(expected);

  fprintf(stderr, "Expected token type %s on line %d, but got %s instead.\n", expected_name, (int)token.line + 1,
          recv_name);
}

}  // namespace ini

std::unique_ptr<Config> Config::Load(const char* file_path) {
  using namespace ini;

  std::unique_ptr<Config> result = std::make_unique<Config>();

  result->filepath = file_path;

  std::string contents = LoadEntireFile(file_path);

  if (contents.empty()) return {};

  Lexer lexer(contents);

  Token token = lexer.GetNextToken();
  Token current_group;

  while (token.type != TokenType::Eof) {
    if (token.type == TokenType::BracketOpen) {
      Token group_id = lexer.GetNextToken();

      if (group_id.type != TokenType::String) {
        EmitTokenError(group_id, TokenType::String);
        return {};
      }

      Token group_end = lexer.GetNextToken();
      if (group_end.type != TokenType::BracketClose) {
        EmitTokenError(group_end, TokenType::BracketClose);
        return {};
      }

      current_group = group_id;
    } else if (token.type == TokenType::String) {
      Token next_token = lexer.GetNextToken();

      // Coalesce Key strings by consuming tokens and adjusting the final token to span the consumed tokens.
      // This allows for spaces in key names.
      {
        Token prev_token = token;

        while (next_token.type == TokenType::String) {
          prev_token = next_token;
          next_token = lexer.GetNextToken();
        }

        const char* key_start_ptr = token.data.data();
        const char* key_end_ptr = prev_token.data.data() + prev_token.data.size();
        size_t key_size = (size_t)(key_end_ptr - key_start_ptr);

        token.data = std::string_view(key_start_ptr, key_size);
      }

      Token equals_token = next_token;

      if (equals_token.type != TokenType::Equals) {
        EmitTokenError(equals_token, TokenType::Equals);
        return {};
      }

      Token value_token = lexer.GetNextToken(true, true);
      if (value_token.type != TokenType::String) {
        EmitTokenError(value_token, TokenType::String);
        return {};
      }

      ConfigGroup& group = result->GetOrCreateGroup(std::string(current_group.data));

      std::string key(Trim(token.data));
      std::string value(Trim(value_token.data));

      group.map[key] = value;
    }

    token = lexer.GetNextToken();
  }

  return result;
}

}  // namespace zero
