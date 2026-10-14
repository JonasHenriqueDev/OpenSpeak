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

## Testar sem o Discord

Abra `https://<servidor>/activity.html` no navegador. Use `?profile=nome` para
ter outra identidade na mesma máquina.
