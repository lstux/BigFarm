#include "bf_web.h"

#include <vector>

#include "bf_system.h"

namespace bf {

struct NavLink {
  const char *href;
  const char *label;
};
static std::vector<NavLink> gNav;

void addNavLink(const char *href, const char *label) {
  NavLink l;
  l.href = href;
  l.label = label;
  gNav.push_back(l);
}

String htmlEscape(const String &s) {
  String out;
  out.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    const char c = s[i];
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default: out += c;
    }
  }
  return out;
}

static const char kStyle[] PROGMEM = R"CSS(
body{font-family:system-ui,sans-serif;margin:0;background:#f4f6f5;color:#1c2421}
header{background:#1fbf8f;color:#fff;padding:12px 16px}
header h1{margin:0;font-size:1.2em}
nav a{color:#fff;margin-right:14px;font-size:.9em}
main{padding:12px 16px;max-width:900px}
section{background:#fff;border-radius:8px;padding:10px 14px;margin:0 0 12px;box-shadow:0 1px 2px #0002}
h2{font-size:1em;margin:4px 0 8px;text-transform:capitalize}
table{border-collapse:collapse;width:100%;font-size:.9em}
th{text-align:left;font-weight:500;color:#556;padding:3px 8px 3px 0;width:40%;vertical-align:top}
td{padding:3px 0;word-break:break-word}
td table th{width:auto}
button,input{font:inherit;padding:8px 12px;border-radius:6px;border:1px solid #bbb}
button{background:#1fbf8f;color:#fff;border:0;cursor:pointer}
button.second{background:#888}
label{display:block;margin:8px 0 4px}
img{max-width:100%;border-radius:6px}
)CSS";

String pageHeader(const String &title) {
  String h;
  h.reserve(2048);
  h += F("<!doctype html><html lang=\"fr\"><head><meta charset=\"utf-8\">"
         "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>");
  h += htmlEscape(title);
  h += F("</title><style>");
  h += FPSTR(kStyle);
  h += F("</style></head><body><header><h1>");
  h += htmlEscape(title);
  h += F("</h1><nav>");
  if (gNav.empty()) h += F("<a href=\"/\">Diagnostic</a>");
  for (size_t i = 0; i < gNav.size(); i++) {
    h += "<a href=\"";
    h += gNav[i].href;
    h += "\">";
    h += gNav[i].label;
    h += "</a>";
  }
  h += F("<a href=\"/wifi\">Wi-Fi</a><a href=\"/api/diag\">JSON</a></nav></header><main>");
  return h;
}

String pageFooter() {
  String f(F("<p style=\"color:#889;font-size:.8em\">"));
  f += htmlEscape(moduleId());
  f += F(" · firmware ");
  f += htmlEscape(firmwareVersion());
  f += F("</p></main></body></html>");
  return f;
}

static const char kDiagScript[] PROGMEM = R"JS(
<div id="d">Chargement…</div>
<script>
function esc(s){return String(s).replace(/[&<>"]/g,function(c){return{'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c];});}
function lab(k){return esc(k.replace(/_/g,' '));}
function cell(v){
  if(v===null||v===undefined)return '—';
  if(typeof v==='boolean')return v?'oui':'non';
  if(Array.isArray(v))return v.length?v.map(function(x){return typeof x==='object'?tbl(x):esc(x);}).join(', '):'—';
  if(typeof v==='object')return tbl(v);
  return esc(v);
}
function tbl(o){var h='<table>';for(var k in o)h+='<tr><th>'+lab(k)+'</th><td>'+cell(o[k])+'</td></tr>';return h+'</table>';}
function charger(){
  fetch('/api/diag').then(function(r){return r.json();}).then(function(d){
    var h='';
    for(var k in d)h+='<section><h2>'+lab(k)+'</h2>'+cell(d[k])+'</section>';
    document.getElementById('d').innerHTML=h;
  }).catch(function(e){document.getElementById('d').textContent='Erreur : '+e;});
}
charger();
</script>
)JS";

void sendDiagPage(WebServer &server, const char *extraHtml) {
  String page = pageHeader(String(moduleLabel()) + " — diagnostic");
  if (extraHtml != nullptr) page += extraHtml;
  page += F("<p><button onclick=\"charger()\">Actualiser</button></p>");
  page += FPSTR(kDiagScript);
  page += pageFooter();
  server.send(200, "text/html; charset=utf-8", page);
}

}  // namespace bf
