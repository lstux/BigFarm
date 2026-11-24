#pragma once

#include <Arduino.h>

#include <functional>
#include <vector>

namespace bf {

// Petite console sur le port série : une commande par ligne, "aide" liste les commandes.
class SerialConsole {
 public:
  typedef std::function<void(const String &args)> Handler;

  void add(const char *name, const char *help, Handler handler);
  void loop(Stream &stream);

  // Découpe "\"Mon réseau\" motdepasse" en deux : premier mot (guillemets possibles) et reste.
  static void splitFirst(const String &args, String &first, String &rest);

 private:
  struct Command {
    String name;
    String help;
    Handler handler;
  };
  void run(Stream &stream, const String &line);

  std::vector<Command> commands_;
  String line_;
};

}  // namespace bf
