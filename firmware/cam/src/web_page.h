#pragma once

#include <Arduino.h>

// Page « Caméra » du module : flux en direct, photo et réglages.
// Le flux est lu sur le port 81 du même module.
static const char kCameraPageHtml[] PROGMEM = R"HTML(
<style>
[hidden]{display:none!important}
.live{background:#111;border-radius:6px;min-height:120px;display:flex;align-items:center;justify-content:center;color:#aaa}
.live img{display:block;max-width:100%}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(230px,1fr));gap:4px 18px}
.grid label{margin:6px 0}
.grid input[type=range]{width:100%;padding:0}
.chk{display:flex;gap:8px;align-items:center}
#msg{min-height:1.2em;color:#556}
.err{color:#c0392b!important}
</style>
<section><h2>Direct</h2>
<div class="live"><img id="flux" alt="Flux de la caméra"><span id="arret" hidden>Flux arrêté</span></div>
<p><button id="bflux">Arrêter le flux</button> <button id="bphoto">Prendre une photo</button>
<span id="stat"></span></p>
<div id="photoBox" hidden><img id="photo" alt="Photo"><p><a id="dl" download="photo.jpg" href="#">Télécharger la photo</a></p></div>
</section>
<section><h2>Réglages</h2>
<form id="f" onsubmit="return false">
<div class="grid">
<label>Résolution<br><select name="resolution" id="resolution"></select></label>
<label>Qualité JPEG : <b id="v_qualite"></b> (petit = meilleure image)<input type="range" name="qualite" min="4" max="63"></label>
<label>Luminosité : <b id="v_luminosite"></b><input type="range" name="luminosite" min="-2" max="2"></label>
<label>Contraste : <b id="v_contraste"></b><input type="range" name="contraste" min="-2" max="2"></label>
<label>Saturation : <b id="v_saturation"></b><input type="range" name="saturation" min="-2" max="2"></label>
<label>LED blanche : <b id="v_led"></b><input type="range" name="led" min="0" max="255"></label>
<label>Images/s max (0 = sans limite)<br><input type="number" name="ips_max" min="0" max="30"></label>
</div>
<div class="grid">
<label class="chk"><input type="checkbox" name="miroir_vertical">Retourner verticalement</label>
<label class="chk"><input type="checkbox" name="miroir_horizontal">Miroir horizontal</label>
<label class="chk"><input type="checkbox" name="balance_blancs_auto">Balance des blancs auto</label>
<label class="chk"><input type="checkbox" name="exposition_auto">Exposition auto</label>
<label class="chk"><input type="checkbox" name="gain_auto">Gain auto</label>
</div>
</form>
<p id="msg"></p>
</section>
<script>
(function(){
var urlFlux=location.protocol+'//'+location.hostname+':81/stream';
var flux=document.getElementById('flux'), arret=document.getElementById('arret');
var bflux=document.getElementById('bflux'), f=document.getElementById('f'), msg=document.getElementById('msg');
var actif=false, resolution=null;
function demarrer(){flux.src=urlFlux+'?t='+Date.now();flux.hidden=false;arret.hidden=true;actif=true;bflux.textContent='Arrêter le flux';}
function arreter(){flux.src='';flux.removeAttribute('src');flux.hidden=true;arret.hidden=false;actif=false;bflux.textContent='Démarrer le flux';}
bflux.onclick=function(){actif?arreter():demarrer();};
document.getElementById('bphoto').onclick=function(){
  var u='/capture?t='+Date.now();
  document.getElementById('photo').src=u;
  document.getElementById('dl').href=u;
  document.getElementById('photoBox').hidden=false;
};
function afficherValeurs(){
  ['qualite','luminosite','contraste','saturation','led'].forEach(function(n){
    document.getElementById('v_'+n).textContent=f.elements[n].value;});
}
function remplir(d){
  var sel=document.getElementById('resolution');
  if(!sel.options.length){d.resolutions.forEach(function(r){var o=document.createElement('option');o.value=r.id;o.textContent=r.nom;sel.appendChild(o);});}
  var r=d.reglages;
  for(var k in r){var e=f.elements[k];if(!e)continue;if(e.type==='checkbox')e.checked=r[k];else e.value=r[k];}
  resolution=r.resolution;
  afficherValeurs();
}
function stats(d){
  var t='';
  if(d.flux){t=d.flux.clients+' client(s)';d.flux.connexions.forEach(function(c){t+=' · '+c.ip+' : '+c.ips+' img/s';});}
  document.getElementById('stat').textContent=t;
}
function charger(){fetch('/api/camera').then(function(r){return r.json();}).then(function(d){remplir(d);stats(d);})
  .catch(function(e){msg.textContent='Erreur : '+e;msg.className='err';});}
var envoiEnCours=null;
function envoyer(){
  var p=new URLSearchParams();
  for(var i=0;i<f.elements.length;i++){var e=f.elements[i];if(!e.name)continue;p.append(e.name,e.type==='checkbox'?(e.checked?'1':'0'):e.value);}
  msg.textContent='Envoi…';msg.className='';
  fetch('/api/camera',{method:'POST',body:p}).then(function(r){return r.json().then(function(d){return{ok:r.ok,d:d};});})
  .then(function(x){
    if(!x.ok){msg.textContent=x.d.erreur||'Erreur';msg.className='err';charger();return;}
    msg.textContent='Réglages appliqués et enregistrés.';
    var changee=(x.d.reglages.resolution!==resolution);
    remplir(x.d);
    if(changee&&actif)demarrer();
  }).catch(function(e){msg.textContent='Erreur : '+e;msg.className='err';});
}
f.addEventListener('input',afficherValeurs);
f.addEventListener('change',function(){clearTimeout(envoiEnCours);envoiEnCours=setTimeout(envoyer,250);});
charger();demarrer();
setInterval(function(){fetch('/api/camera').then(function(r){return r.json();}).then(stats).catch(function(){});},3000);
})();
</script>
)HTML";
