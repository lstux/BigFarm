#pragma once

#include <Arduino.h>
#include <WebServer.h>

namespace bf {

String htmlEscape(const String &s);

// En-tête et pied de page HTML communs (style sobre, lisible sur téléphone).
String pageHeader(const String &title);
String pageFooter();

// Page de diagnostic générique : elle lit /api/diag et l'affiche en tableaux.
// extraHtml est inséré avant les tableaux (boutons propres au module).
void sendDiagPage(WebServer &server, const char *extraHtml);

}  // namespace bf
