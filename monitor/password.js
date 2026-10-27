// Senha do servidor: o openspeak-server relê o arquivo a cada login.
(()=>{
const field=document.getElementById('password'),status=document.getElementById('passwordStatus'),save=document.getElementById('passwordSave'),toggle=document.getElementById('passwordToggle');
let saved='';
function show(text){status.textContent=text}
async function call(options={}){const r=await fetch('/monitor/api/password',{cache:'no-store',signal:AbortSignal.timeout(8000),...options}),d=await r.json().catch(()=>({}));if(!r.ok)throw Error(d.error||('Erro '+r.status));return d}
async function load(){try{const d=await call();saved=field.value=d.password;field.placeholder='Nova senha';show(saved?'Servidor privado: o login exige esta senha.':'Nenhuma senha definida no painel: vale OPENSPEAK_PASSWORD, se configurada no serviço.')}catch(e){field.placeholder='Indisponível';show('Não foi possível ler a senha: '+e.message)}}
toggle.addEventListener('click',()=>{const hidden=field.type==='password';field.type=hidden?'text':'password';toggle.textContent=hidden?'Ocultar':'Mostrar'});
document.getElementById('passwordForm').addEventListener('submit',async ev=>{ev.preventDefault();const password=field.value.trim();if(password===saved){show('A senha não mudou.');return}save.disabled=true;show('Salvando…');try{const d=await call({method:'POST',headers:{'Content-Type':'application/json','X-OpenSpeak-Monitor':'1'},body:JSON.stringify({password})});saved=field.value=d.password;show('Senha alterada. Vale a partir do próximo login; avise quem for entrar.')}catch(e){show('Não foi salva: '+e.message)}finally{save.disabled=false}});
load();
})();
