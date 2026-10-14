# Atividade do Discord (somente assistir)

O OpenSpeak pode ser aberto como uma atividade num canal de voz do Discord para
assistir às transmissões do servidor sem sair do Discord.

## Como funciona

Atividades rodam num iframe do Discord e todo o tráfego passa pelo proxy dele,
que só aceita HTTP e WebSocket: **WebRTC não funciona**. Por isso a atividade não
usa o player normal:

1. `index.html` detecta os parâmetros do Discord (`frame_id`, `instance_id`) e vai
   para `activity.html`.
2. A atividade entra no servidor pelo WebSocket de sinalização (`/ws`) e pede para
   assistir com `joinstreamrequest { transport: "websocket" }`.
3. O relay SFU (`SfuWsViewer`) repassa o mesmo RTP H.264 dos espectadores WebRTC,
   em lotes binários, pelo próprio WebSocket.
4. O navegador remonta os quadros (`js/activity/rtpH264.js`) e decodifica com
   WebCodecs num `<canvas>` (`js/activity/WsVideoPlayer.js`).

O player mostra a imagem inteira, com faixas onde a proporção da transmissão não
bate com a área que o Discord dá à atividade. O botão **Preencher** (ou duplo
clique no vídeo) corta as bordas para ocupar a área toda.

O Discord bloqueia a Fullscreen API no iframe da atividade e, mesmo na tela cheia
dele, reserva faixas em cima e embaixo para o cabeçalho e os controles da chamada
(numa tela 1920×1080 a atividade recebe ~1920×840). Por isso o botão de tela
cheia não aparece dentro do Discord; tela cheia de verdade só no app ou na web.

Limitações do protótipo:

- Só transmissões no modo **Servidor (SFU)**; P2P aparece desabilitado.
- Áudio: Opus decodificado com WebCodecs, com buffer de ~120 ms; a imagem espera o mesmo tempo para ficar sincronizada.
- TCP: perda de rede gera uma pausa curta até o próximo keyframe, em vez de
  queda de qualidade. Com o socket congestionado (> 512 KB na fila), o servidor
  descarta vídeo até o próximo keyframe para não acumular atraso.
- Espectadores da atividade não entram no controle de banda do SFU.

## Configurar no Discord Developer Portal

1. Crie um aplicativo em <https://discord.com/developers/applications>.
2. **Activities → Settings**: habilite as atividades.
3. **Activities → URL Mappings**: `/` → `openspeak.duckdns.org` (o servidor nativo
   com `--web`, atrás do Caddy com HTTPS).
4. **Installation**: instale o aplicativo no seu servidor do Discord.
5. Num canal de voz, abra o lançador de atividades e escolha o aplicativo.

O servidor precisa estar com a versão que tem o `SfuWsViewer`.

Ícone 1024×1024 para o portal: `docs/discord-app-icon-1024.png`.

## Nome do Discord como apelido (desligado)

Por padrão a atividade pede o nome (e tem **Trocar nome** na lista de
transmissões). Para ligar o login com a conta do Discord, além dos passos abaixo,
defina `discordLogin: true` no `config.js` servido pelo servidor.

Com ele ligado, a atividade entra com o nome de exibição da conta, no formato
`Nome (discord)`. O SDK só libera o usuário depois de um login OAuth2 (escopo
`identify`), e trocar o código pelo token exige o **client secret**, que fica só
no servidor (`POST /api/discord/token`).

1. No portal, em **OAuth2**, copie o **Client ID** e gere o **Client Secret**. Em
   **Redirects**, adicione `https://127.0.0.1` (a atividade não usa, mas o Discord
   exige ao menos um).
2. Na VM, coloque as credenciais em `/etc/openspeak-server.env` (só root lê):

   ```sh
   DISCORD_CLIENT_ID=<client id>
   DISCORD_CLIENT_SECRET=<client secret>
   ```

3. `sudo systemctl restart openspeak`. O log mostra `login do Discord: app <id>`.

Na primeira vez o Discord pede ao usuário para autorizar o app. Sem as
credenciais, ou se o usuário recusar, a atividade volta a pedir o nome.

O HTTPS do servidor para a API do Discord usa o plugin de TLS do Qt
(`tls/libqopensslbackend.so`, ao lado do binário) com o OpenSSL do sistema.

## Testar sem o Discord

Abra `https://<servidor>/activity.html` no navegador. Use `?profile=nome` para
ter outra identidade na mesma máquina.
