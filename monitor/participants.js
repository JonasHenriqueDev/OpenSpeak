// Protected admin UI. Render user-controlled fields only through textContent.
const expandedPeople = new Set();
let peopleRoom = null;
const stateLabels = {new:'Iniciando',connecting:'Conectando',connected:'Conectado',reconnecting:'Reconectando',failed:'Falhou',closed:'Encerrado'};
const participantElement = (tag, text, className='') => {
  const el = document.createElement(tag);
  if (text !== undefined) el.textContent = text;
  if (className) el.className = className;
  return el;
};
const line = (cell, text, muted=false) => cell.append(participantElement('div', text, muted ? 'detail' : 'person-line'));
const duration = seconds => {
  seconds = Math.floor(seconds || 0);
  const h=Math.floor(seconds/3600),m=Math.floor(seconds%3600/60),s=seconds%60;
  return h ? `${h}h ${m}min` : m ? `${m}min ${s}s` : `${s}s`;
};
const mediaRate = value => value == null ? 'Coletando…' : fmt(mbps(value))+' Mbps';

function renderPeople(room) {
  peopleRoom = room;
  const body = $('people'), query=$('peopleSearch').value.trim().toLowerCase();
  const channels=new Map((room.channels||[]).map(c=>[c.cid,c.name]));
  const shown=room.clients.filter(c=>[c.nickname,c.ip,c.client?.name,c.client?.platform,...(c.activities||[]).map(a=>a.name)].join(' ').toLowerCase().includes(query));
  put('peopleCount',shown.length+' / '+room.clients.length);
  const focusedId=document.activeElement?.dataset?.clientId;
  body.replaceChildren();
  if(!shown.length){const tr=participantElement('tr'),td=participantElement('td',room.online?(query?'Nenhum participante corresponde à busca':'Nenhuma pessoa conectada'):'OpenSpeak indisponível','empty');td.colSpan=7;tr.append(td);body.append(tr);return}
  for(const c of shown) {
    const tr=participantElement('tr');
    const cells=Array.from({length:7},()=>participantElement('td'));
    const client=c.client||{name:'Não identificado',platform:'',version:'',source:''};
    line(cells[0],c.nickname);
    line(cells[0],(channels.get(c.cid)||'Canal '+c.cid)+' · #'+c.clid,true);
    line(cells[1],client.name+(client.version?' '+client.version:''));
    line(cells[1],client.platform||'Sistema não informado',true);
    cells[1].title=client.source;
    line(cells[2],c.ip||'Não informado');
    line(cells[2],'IP de sinalização',true);
    const mediaIps=[...new Set((c.activities||[]).map(a=>a.mediaIp).filter(Boolean))];
    for(const ip of mediaIps) if(ip!==c.ip) line(cells[2],'Mídia: '+ip,true);
    if(!c.activities?.length) line(cells[3],'Na sala · sem transmissão',true);
    for(const a of c.activities||[]){
      line(cells[3],(a.role==='publisher'?'Transmite: ':'Assiste: ')+a.name);
      line(cells[3],(a.mode===2?'SFU':'P2P')+' · '+(a.mode===2?(stateLabels[a.state]||'Estado indisponível'):'Inscrito')+(a.paused?' · pausada':''),true);
    }
    const hasSfu=(c.activities||[]).some(a=>a.mode===2);
    if(hasSfu){
      line(cells[4],'↑ '+mediaRate(c.sfuUploadBytesPerSecond));
      line(cells[4],'↓ '+mediaRate(c.sfuDownloadBytesPerSecond));
      line(cells[4],'RTP nas sessões SFU atuais',true);
    } else line(cells[4],c.hasP2P?'P2P: não medido':'Sem mídia SFU',true);
    if(hasSfu&&c.hasP2P) line(cells[4],'P2P adicional: não medido',true);
    line(cells[5],duration(c.connectedSeconds));
    line(cells[5],c.pingRttMs==null?'RTT: aguardando ping':'RTT sinalização: '+fmt(c.pingRttMs)+' ms',true);
    const button=participantElement('button',expandedPeople.has(c.clid)?'Fechar':'Detalhes','details-button');
    button.type='button';button.dataset.clientId=String(c.clid);
    button.setAttribute('aria-label','Detalhes de '+c.nickname);
    button.setAttribute('aria-expanded',String(expandedPeople.has(c.clid)));
    cells[6].append(button);tr.append(...cells);body.append(tr);
    if(expandedPeople.has(c.clid)) {
      const extra=participantElement('tr'),td=participantElement('td');td.colSpan=7;
      const grid=participantElement('div',undefined,'person-details');
      function item(label,value){const box=participantElement('div');box.append(participantElement('div',label,'label'),participantElement('div',value,'person-line'));grid.append(box)}
      item('Conectado desde',c.connectedAt?date(c.connectedAt/1000):'Não informado');
      item('Último contato / heartbeat',c.lastActivityAt?date(c.lastActivityAt/1000):'Não informado');
      item('Sinalização acumulada · ↑ envio / ↓ recebimento',bytes(c.signalingRxBytes||0)+' / '+bytes(c.signalingTxBytes||0));
      item('Sinalização agora · ↑ envio / ↓ recebimento',mediaRate(c.signalingRxBytesPerSecond)+' / '+mediaRate(c.signalingTxBytesPerSecond));
      item('Mídia SFU acumulada nas sessões atuais · ↑ / ↓',bytes(c.sfuUploadBytes||0)+' / '+bytes(c.sfuDownloadBytes||0));
      item('Identificação do cliente',client.source||'Não informada');
      if(c.userAgent) item('User-Agent',c.userAgent);
      if(c.origin) item('Origem web',c.origin);
      for(const a of c.activities||[]) {
        const details=[];
        details.push(a.mode===2?'SFU · '+(stateLabels[a.state]||'Estado indisponível'):'P2P · inscrição registrada');
        if(a.width&&a.height) details.push('Alvo: '+a.width+'×'+a.height+' · '+a.fps+' fps'+(a.audio?' · áudio':''));
        if(a.mode===2){
          if(a.mediaIp) details.push('Endpoint de mídia: '+a.mediaIp+(a.mediaPort?':'+a.mediaPort:''));
          if(a.connectionType) details.push('ICE: '+a.connectionType);
          if(a.generation>1) details.push('Reconexões: '+(a.generation-1));
          if(a.rttMs!=null) details.push('RTT mídia: '+fmt(a.rttMs)+' ms');
          if(a.lossPercent!=null) details.push('Perda vídeo: '+fmt(a.lossPercent)+'%');
          if(a.jitterMs!=null) details.push('Jitter vídeo: '+fmt(a.jitterMs)+' ms');
          if(a.nackedPackets!=null) details.push('Pacotes solicitados por NACK: '+a.nackedPackets);
          if(a.role==='publisher') details.push('Perda/RTT de ingestão não disponíveis');
        } else details.push('Tráfego, endpoint e qualidade P2P não observáveis na VM');
        item((a.role==='publisher'?'Transmitindo: ':'Assistindo: ')+a.name+' · '+a.streamId,details.join(' · '));
      }
      td.append(grid);extra.append(td);body.append(extra);
    }
  }
  if(focusedId) body.querySelector('[data-client-id="'+focusedId+'"]')?.focus({preventScroll:true});
}

$('people').addEventListener('click',event=>{
  const button=event.target.closest('button[data-client-id]');if(!button)return;
  const id=Number(button.dataset.clientId);
  expandedPeople.has(id)?expandedPeople.delete(id):expandedPeople.add(id);
  renderPeople(peopleRoom);
});
$('peopleSearch').addEventListener('input',()=>{if(peopleRoom)renderPeople(peopleRoom)});
loop();
