# OpenSpeak

Transmissão de vídeo P2P (tela, janela ou câmera, com áudio) inspirada no motor e na
interface de streaming do TeamSpeak 6. A análise do cliente original está em
[docs/ANALISE_TEAMSPEAK_P2P.md](docs/ANALISE_TEAMSPEAK_P2P.md).

| | |
|---|---|
| **App nativo Windows** (Qt 6 + QML Fluent, libdatachannel, FFmpeg/NVENC) | [native/](native/README.md): instalador `.exe`, **modo Tailnet sem servidor** |
| Cliente web + servidor Node | esta página |

## App nativo (recomendado)

Instale o `OpenSpeak-Setup-<versão>.exe` e o [Tailscale](https://tailscale.com/download/windows).
Abra o app e entre: todos da mesma tailnet aparecem automaticamente, sem IP nem
servidor. Detalhes em [native/README.md](native/README.md).

## Cliente web

### Rodar

```bash
npm install
npm start
```

Abra `http://localhost:9988`. Para testar sozinho, abra duas abas com perfis diferentes:

- `http://localhost:9988/?profile=a&debug`
- `http://localhost:9988/?profile=b&debug`

Com `?debug`, o diálogo de transmissão ganha a aba **Teste**, um padrão animado com tom de
áudio. Ela permite validar o motor sem o seletor de tela do sistema.

### Rede local / internet

Fora de `localhost`, o navegador só libera captura de tela e câmera em HTTPS:

```bash
OPENSPEAK_CERT=cert.pem OPENSPEAK_KEY=key.pem npm start
```

Variáveis: `PORT` (9988), `HOST` (0.0.0.0), `OPENSPEAK_NAME`, e `OPENSPEAK_ICE`, que recebe
um JSON de `iceServers`, por exemplo para adicionar TURN em redes com NAT simétrico.

## Arquitetura

| TeamSpeak 6 (nativo) | OpenSpeak |
|---|---|
| comandos `setupstream`, `joinstreamrequest`, `streamsignaling`… | [server/virtualServer.js](server/virtualServer.js) (mesmos nomes) |
| `ScreenshareManager` | [ScreenshareManager.js](public/js/video/ScreenshareManager.js) |
| `StreamBase` (kOffer/kAnswer/kICECandidate/kReconnect) | [StreamBase.js](public/js/video/StreamBase.js) |
| `SessionOutbound` / `SessionInbound` | [SessionOutbound.js](public/js/video/SessionOutbound.js) / [SessionInbound.js](public/js/video/SessionInbound.js) |
| `ScalableSharedVideoTransformer` + capturers | [SharedVideoSource.js](public/js/video/SharedVideoSource.js) |
| seleção dinâmica de codec | [codecs.js](public/js/video/codecs.js) |
| `streamStatsSummary` + detecção de problemas | [StatsCollector.js](public/js/video/StatsCollector.js) (mesmos limiares) |
| layouts grade/foco | [layout.js](public/js/video/layout.js) |
| UI (setup, tiles, espectadores, stats) | [public/js/ui/](public/js/ui/) |

Fluxo: transmissor → `setupstream` → espectador → `joinstreamrequest` (com codecs que sabe
decodificar) → privacidade (Público aceita direto, Contatos aceita contatos, Privado pergunta)
→ `respondjoinstreamrequest` → o transmissor cria uma `SessionOutbound` e oferta → SDP e ICE
passam por `streamsignaling` → a mídia vai direto entre os pares (DTLS-SRTP).

### Diferenças em relação ao TS

- **Single-encoding:** o TS codifica uma vez para N espectadores. No navegador cada
  `RTCRtpSender` codifica. A captura é compartilhada (uma track para todos) e bitrate/FPS
  são aplicados por espectador.
- O modo **SFU** aparece desabilitado, porque o servidor só anuncia `STREAM_P2P`.
- A janela ou tela é escolhida pelo seletor nativo do navegador.

## Testes

```bash
npm test
```
