# Web + Windows pela tailnet

A página do Vercel é a entrada pública. Ao conectar, ela abre a interface no
endereço privado HTTPS do servidor Tailscale. Isso evita a conexão entre a página
pública e a rede privada, que alguns navegadores bloqueiam. O site privado serve
o mesmo cliente web do projeto. Nenhum vídeo passa pelo Vercel.

Todos os PCs devem executar Tailscale e pertencer à mesma tailnet (com acesso
permitido pelas regras dela). Um PC hospeda o servidor de sinalização e precisa
permanecer ligado enquanto o grupo usa o app. O modo de descoberta automática
da malha do app nativo continua disponível; para interagir com o navegador, os
apps nativos devem conectar ao servidor dedicado comum.

## Hospedar

Gere a release com `native/package.ps1` e execute:

```powershell
.\native\start-tailnet-server.ps1
```

Esse script abre o servidor em `127.0.0.1:9988`, sem STUN/TURN, e configura
Tailscale Serve em HTTPS. Se a tailnet ainda não tiver HTTPS/Serve habilitado,
o próprio Tailscale informa a página para habilitar. Não é usado Tailscale Funnel.
Nenhuma tarefa de inicialização do Windows é criada automaticamente.

No navegador, abra a página do Vercel e conecte. No `.exe`, selecione
**Servidor dedicado (avançado)** e use `wss://<nome-do-pc>.<tailnet>.ts.net/ws`.

## Publicar a entrada

```powershell
.\scripts\build-web.ps1 -ServerUrl 'wss://<nome-do-pc>.<tailnet>.ts.net/ws'
```

Publique a pasta `web-dist` no Vercel. Ela contém apenas HTML, CSS e JavaScript;
não há funções, Redis, servidor TURN ou mídia no Vercel. Para atualização via CLI:

```powershell
cd web-dist
npx vercel --prod --scope jonashenriquedevs-projects
```

## Testar no mesmo PC

```powershell
.\OpenSpeak-Portable-0.2.2.exe --local-test
```

Isso abre Emissor e Espectador com configurações separadas e um servidor de teste
local na porta 19988. Feche as duas janelas para encerrar o servidor e limpar os
arquivos temporários. Não use duas cópias desse teste simultaneamente.

Para testar o navegador, transmita uma janela ou o padrão de teste no nativo e
assista no web; depois faça o inverso. Fones ajudam a evitar eco. Em um único PC,
esses testes verificam protocolo, captura, decodificação e troca de fonte, mas
não verificam CGNAT real ou o caminho entre máquinas diferentes.

## Limites

- Sem Tailscale, a sessão privada não abre. Sem TURN, não há fallback pela internet.
- O Tailscale pode usar DERP se a conexão direta falhar; nesse caso a mídia passa
  pelo relay criptografado do Tailscale, nunca pelo Vercel.
- O nativo usa seu endereço Tailscale para mídia em servidores ts.net. O web usa
  candidatos host da tailnet/mDNS e não configura STUN/TURN. O navegador decide
  quais interfaces expor. A interoperabilidade web/nativo deve ser testada também
  em dois PCs; navegador/navegador entre PCs pode ser limitado por mDNS.
- As configurações do portátil são as mesmas do app instalado, no perfil do
  Windows; o arquivo único não transporta automaticamente seus dados para outro PC.
- A extração usa o PowerShell incluído no Windows. Políticas corporativas que
  bloqueiam PowerShell podem impedir a abertura do pacote de arquivo único.
