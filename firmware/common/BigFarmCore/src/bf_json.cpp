#include "bf_json.h"

namespace bf {

static void newLine(Print &out, int indent) {
  out.print('\n');
  for (int i = 0; i < indent; i++) out.print("  ");
}

void printJsonPretty(Print &out, const String &json) {
  int indent = 0;
  bool inString = false;
  bool escaped = false;
  for (size_t i = 0; i < json.length(); i++) {
    const char c = json[i];
    if (inString) {
      out.print(c);
      if (escaped) {
        escaped = false;
      } else if (c == '\\') {
        escaped = true;
      } else if (c == '"') {
        inString = false;
      }
      continue;
    }
    switch (c) {
      case '"':
        inString = true;
        out.print(c);
        break;
      case '{':
      case '[':
        out.print(c);
        indent++;
        newLine(out, indent);
        break;
      case '}':
      case ']':
        if (indent > 0) indent--;
        newLine(out, indent);
        out.print(c);
        break;
      case ',':
        out.print(c);
        newLine(out, indent);
        break;
      case ':':
        out.print(": ");
        break;
      default:
        out.print(c);
    }
  }
  out.print('\n');
}

}  // namespace bf
