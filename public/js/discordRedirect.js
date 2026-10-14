// Aberto como atividade do Discord (URL mapping "/" -> este servidor): vai para
// o player só de assistir, que não depende de WebRTC.
(function () {
  var params = new URLSearchParams(location.search);
  if (params.get('frame_id') && params.get('instance_id')) location.replace('activity.html' + location.search);
})();
