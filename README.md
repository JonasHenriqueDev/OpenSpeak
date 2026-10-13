# OpenSpeak

Transmissão de vídeo P2P (tela, janela ou câmera, com áudio) inspirada no motor e na
interface de streaming do TeamSpeak 6. A análise do cliente original está em
[docs/ANALISE_TEAMSPEAK_P2P.md](docs/ANALISE_TEAMSPEAK_P2P.md).

| | |
|---|---|
| **App nativo Windows** (Qt 6 + QML Fluent, libdatachannel, FFmpeg/NVENC) | [native/](native/README.md): instalador `.exe`, servidor e modos avançados |
| Cliente web + servidor Node | esta página |

## App nativo (recomendado)

Baixe o [instalador da última release estável](https://github.com/JonasHenriqueDev/OpenSpeak/releases/latest/download/OpenSpeak-Setup.exe).
A partir da 1.0.0, o app instalado atualiza automaticamente e oferece um canal
opcional de pré-releases. [Downloads e atualizações](docs/ATUALIZACOES.md).

Instale o `OpenSpeak-Setup-<versão>.exe`, informe seu nome e conecte ao servidor.
O endereço padrão é `wss://openspeak.duckdns.org/ws`. Tailscale e sala web ficam
em **Avançado**; Tailscale é necessário somente para esses modos.
Detalhes em [native/README.md](native/README.md).

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

## Licença

OpenSpeak é software livre sob a [GNU AGPL v3.0 ou posterior](LICENSE). Se você hospedar
uma versão modificada do servidor, precisa disponibilizar o código-fonte dela aos usuários.
Licenças das dependências: [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).
