# Análise: transmissão de vídeo P2P do TeamSpeak 6

Alvo: `C:\Users\Jonas\AppData\Local\Programs\TeamSpeak` (cliente CEF + núcleo nativo).
Ferramenta: REA (`analyze_javascript_application` sobre `html/client_ui`,
evidência `ev_2a43c28f…d208`, cobertura **parcial** — o worker esgotou heap no
`main.js` de 10 MB) complementada por buscas textuais no bundle e por extração
de strings/RTTI do `TeamSpeak.dll`.

Legenda: **[O]** observado no artefato · **[I]** inferência · **[?]** desconhecido.

## 1. Arquitetura

```
 UI (Vue, html/client_ui/main.js)  ──bindings CEF──►  TeamSpeak.dll (C++)
   setup dialog, grid, stats            com::teamspeak::video
                                          ScreenshareManager
                                          StreamBase ─┬─ StreamP2P   (libwebrtc PeerConnection)
                                                      └─ StreamSFU   (mediasoup-like, "Device Load", producer/consumer)
                                          SessionOutbound / SessionInbound / SessionSubscriberManager
                                          ScalableSharedVideoTransformer  (single-encoding)
                                          DesktopCapturer / CameraCapturer / ProcessAudioCapturer / ThumbnailCapturer
                                          FrameScheduler, VideoAudioSyncBuffer, TSFrontBackBuffer (render p/ UI)
```

- **[O]** Nenhum `RTCPeerConnection` no JS; o JS só manipula modelo/config. Todo o
  WebRTC é nativo (≈2 900 ocorrências de `webrtc`, RTTI `SdpOfferAnswerHandler@webrtc`).
- **[O]** Codecs: AV1, VP9, H264 (OpenH264, NVENC, AMF, QSV), VP8; `ScalabilityMode` (ex. `L1T3`).
- **[O]** Changelog no bundle: "true P2P streaming with single-encoding", "dynamic codec
  selection for streams".

## 2. Sinalização (via servidor de voz, não há servidor P2P dedicado)

Comandos cliente → servidor **[O]**: `setupstream`, `updatestream`, `stopstream`,
`requeststreaminfo`, `removeclientfromstream`, `joinstreamrequest`,
`respondjoinstreamrequest`, `streamsignaling`, `requestsfuaccessinfo`.

Notificações servidor → cliente **[O]**: `notifystreamstarted`, `notifystreamstopped`,
`notifystreamupdated`, `notifystreaminfo`, `notifystreamattendees`,
`notifyjoinstreamrequest`, `notifyrespondjoinstreamrequest`,
`notifystreamclientjoined`, `notifystreamclientleft`, `notifystreamsignaling`.

Campos vizinhos no binário **[O]**: `streamIdentifier`, `source`, `access`, `bitrate`,
`viewerLimit`, `return_code`, `viewer`, `streamAttendees`, `streamInfo`.

Payload de `streamsignaling` (`Stream_Signaling_Commands_Base`, nlohmann::json) **[O]**:
`kOffer` (com `is_reconnect`), `kAnswer`, `kICECandidate`, `kReconnect`.
`StreamBase::StartOfferAnswer`, `StreamBase::Reconnect`, `StreamBase::SendToPeer`.

Capacidade do servidor **[O]**: `capabilityExtensions` contém `STREAM_P2P` / `STREAM_SFU`.

## 3. Modelo de stream (JS)

- **[O]** `config = {bitrate, access, mode, viewerLimit, name, type, audio, sfuEndpoint, …}`
- **[O]** enums: `access {NONE, PUBLIC, CONTACTS_ONLY, PRIVATE}`,
  `mode {NONE, P2P, SFU}`, `type {NONE, CAMERA, SCREEN, WINDOW, EXISTING_SESSION}`,
  motivo de saída `{NONE, LEFT, DENIED, FAILED, KICKED, …}`.
- **[O]** Presets: 360 (0.5 Mbps), 480 (1), 720 (3), 1080 (5), 1440 (6.5), Source (8),
  Presentation (2 Mbps @ 5 fps); FPS 5/30/60; áudio 64–320 kbps. Padrão = 720.
- **[O]** Privacidade: Public (miniaturas visíveis), Contacts (contatos auto-aceitos),
  Private (todos pedem para entrar). **[I]** Public = aceite automático.
- **[O]** Gestão de espectadores: assistindo / pedidos / bloqueados; kick, ban, "Block & Deny",
  limite de espectadores.

## 4. Detecção de problemas (limiares exatos [O])

| Métrica | Menor | Maior |
|---|---|---|
| perda de pacotes (fractionLost) | > 0.1 % | > 5 % |
| jitter | > 100 ms | — |
| RTT | > 300 ms | > 600 ms |
| packet delay (sent.currentDelay) | > 5 s | — |
| retransmissão | > 0 bps | — |
| qualityLimitationReason ≠ none | sim | resolução < 33 % do alvo |
| tempo médio de codificação | > 1/fps | > 1.5/fps |
| FPS | < 80 % alvo | < 60 % alvo |

## 5. Layout

- **[O]** Grade automática: testa 1..N linhas, altura mínima 100 px, 16:9, escolhe a
  configuração que mostra mais itens e, no empate, maior área; paginação.
- **[O]** Layout com foco: stream focado grande + faixa de miniaturas abaixo.

## 6. Diferenças inevitáveis na reimplementação (navegador)

- **Single-encoding**: o TS injeta um único fluxo codificado em N PeerConnections
  (`ScalableSharedVideoTransformer`). No navegador cada `RTCRtpSender` codifica; o
  OpenSpeak compartilha a **captura** (uma única track) e adapta bitrate por espectador.
- Seleção de janela/tela usa o seletor nativo do navegador (`getDisplayMedia`).
- SFU não implementado (modo aparece desabilitado, como no TS sem `STREAM_SFU`).
