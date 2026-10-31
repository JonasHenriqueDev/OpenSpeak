// Perfis: lista os apelidos reclamados e gera uma senha nova para quem perdeu a sua.
(()=>{
const list=document.getElementById('profiles'),count=document.getElementById('profilesCount'),status=document.getElementById('profilesStatus');
function show(text){status.textContent=text}
async function call(path,options={}){const r=await fetch('/monitor/api/'+path,{cache:'no-store',signal:AbortSignal.timeout(8000),...options}),d=await r.json().catch(()=>({}));if(!r.ok)throw Error(d.error||('Erro '+r.status));return d}
function cell(text,cls){const td=document.createElement('td');td.textContent=text;if(cls)td.className=cls;return td}
async function reset(nickname,button){if(!confirm(`Gerar uma senha nova para ${nickname}? A senha atual deixa de valer.`))return;button.disabled=true;show('Gerando…');try{const d=await call('profiles/reset',{method:'POST',headers:{'Content-Type':'application/json','X-OpenSpeak-Monitor':'1'},body:JSON.stringify({nickname})});show(`Nova senha de ${d.nickname}: ${d.password} — anote agora, ela não aparece de novo. A pessoa pode trocá-la em Preferências › Perfil.`)}catch(e){show('Não foi gerada: '+e.message)}finally{button.disabled=false}}
async function load(){try{const{profiles}=await call('profiles');count.textContent=profiles.length;list.replaceChildren(...profiles.map(p=>{const tr=document.createElement('tr'),action=document.createElement('td'),button=document.createElement('button');tr.append(cell(p.nickname),cell(p.displayName||'—','detail'));button.type='button';button.className='details-button';button.textContent='Gerar nova senha';button.addEventListener('click',()=>reset(p.nickname,button));action.append(button);tr.append(action);return tr}));if(!profiles.length){const tr=document.createElement('tr'),td=cell('Nenhum perfil cadastrado.','empty');td.colSpan=3;tr.append(td);list.append(tr)}}catch(e){show('Não foi possível ler os perfis: '+e.message)}}
load();setInterval(load,30000);
})();
