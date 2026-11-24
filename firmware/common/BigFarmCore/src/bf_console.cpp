#include "bf_console.h"

namespace bf {

void SerialConsole::add(const char *name, const char *help, Handler handler) {
  Command c;
  c.name = name;
  c.help = help;
  c.handler = handler;
  commands_.push_back(c);
}

void SerialConsole::loop(Stream &stream) {
  while (stream.available()) {
    const char c = static_cast<char>(stream.read());
    if (c == '\r' || c == '\n') {
      if (line_.length()) {
        String line = line_;
        line_ = "";
        run(stream, line);
      }
    } else if (line_.length() < 200) {
      line_ += c;
    }
  }
}

void SerialConsole::run(Stream &stream, const String &raw) {
  String line = raw;
  line.trim();
  String name;
  String args;
  splitFirst(line, name, args);
  stream.printf("> %s\n", name.c_str());

  if (name == "aide" || name == "help" || name == "?") {
    stream.println("Commandes :");
    stream.println("  aide                 cette liste");
    for (size_t i = 0; i < commands_.size(); i++) {
      stream.printf("  %-20s %s\n", commands_[i].name.c_str(), commands_[i].help.c_str());
    }
    return;
  }
  for (size_t i = 0; i < commands_.size(); i++) {
    if (commands_[i].name == name) {
      commands_[i].handler(args);
      return;
    }
  }
  stream.println("Commande inconnue, tapez « aide ».");
}

void SerialConsole::splitFirst(const String &args, String &first, String &rest) {
  String s = args;
  s.trim();
  int end;
  if (s.startsWith("\"")) {
    end = s.indexOf('"', 1);
    if (end < 0) end = s.length();
    first = s.substring(1, end);
    rest = (end + 1 < static_cast<int>(s.length())) ? s.substring(end + 1) : String();
  } else {
    end = s.indexOf(' ');
    if (end < 0) {
      first = s;
      rest = String();
    } else {
      first = s.substring(0, end);
      rest = s.substring(end + 1);
    }
  }
  rest.trim();
}

}  // namespace bf
