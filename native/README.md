# OpenSpeak nativo (Qt 6 + libdatachannel + FFmpeg)

Aplicativo Windows (`OpenSpeak.exe`) e servidor (`openspeak-server.exe`) em C++.
A interface usa QML com o estilo FluentWinUI3. O motor de transmissão P2P
reproduz a arquitetura do TeamSpeak 6 (ver `../docs/ANALISE_TEAMSPEAK_P2P.md`).
Usa o mesmo protocolo do cliente web, então clientes nativos e do navegador
assistem uns aos outros.

## Compilar

Pré-requisitos (já instalados nesta máquina):

| Item | Onde |
|---|---|
| Visual Studio 2026 (C++), com CMake | `C:\Program Files\Microsoft Visual Studio\18\Community` |
| Qt 6.11.3 MSVC 64-bit (Quick, Multimedia, WebSockets, HttpServer, ShaderTools) | `C:\Qt\6.11.3\msvc2022_64` |
| vcpkg + `libdatachannel[srtp]`, `ffmpeg[nvcodec,amf,qsv,openh264,dav1d,opus,…]` | `D:\Dev\vcpkg`, `native\vcpkg_installed` |

```bash
powershell -ExecutionPolicy Bypass -File native/build.ps1
```

O script configura, compila em Release e roda os testes. O `windeployqt` copia as
DLLs do Qt para `build\Release`, que fica autônomo. FFmpeg, libdatachannel e
OpenSSL são linkados estaticamente.

Para reinstalar as dependências (≈30 min na primeira vez):

```bash
cd native && D:/Dev/vcpkg/vcpkg.exe install --triplet x64-windows-static-md --x-install-root=vcpkg_installed
```

## Instalar

Baixe e rode `OpenSpeak-Setup-<versão>.exe`. O instalador já traz o Qt, o FFmpeg, o
WebRTC e o runtime do Visual C++, além de liberar o app no firewall. O **único
requisito externo é o [Tailscale](https://tailscale.com/download/windows)**: instale e
faça login.

Para gerar o instalador, rode o comando abaixo. Ele compila, testa e cria
`native/dist/OpenSpeak-Setup-<versão>.exe`.

```bash
powershell -ExecutionPolicy Bypass -File native/package.ps1
```

## Modo Tailnet (padrão): sem servidor

Abra o app, escolha um **nome de exibição** e clique em **Entrar**. Não há IP nem
servidor para configurar.

1. **Descoberta.** O app lê `tailscale status --json` e descobre as máquinas online da
   tailnet. Em cada uma, tenta a porta `9987`, escutando só no IP `100.x` da tailnet.
2. **Malha.** Quem roda o OpenSpeak entra numa malha (cada app conecta direto nos
   outros). Todos veem quem está em cada sala e quem está transmitindo, e podem assistir.
3. **Autoridade local.** Cada app decide sobre os próprios streams: privacidade, pedidos
   de entrada, limite, kick e ban. Os pedidos e a sinalização WebRTC vão direto para o
   dono do stream.
4. **Identidade.** O nome de exibição é livre. A **identidade real** (usuário e máquina
   do Tailscale, ex.: "Jonas Santos · desktop-5ug0epe") é obtida pelo IP de origem, ou
   seja, verificada pelo WireGuard e impossível de falsificar. Ela aparece de forma
   discreta: no hover da árvore de canais, nos pedidos de entrada e no painel de
   espectadores. Bans e contatos usam essa identidade.

### Servidor dedicado (opcional)

Em "Servidor dedicado (avançado)" na tela de entrada, o app usa um servidor central,
compatível também com o cliente web:

```bash
openspeak-server.exe --port 9988
```

Opções do app:
- `--profile nome`: configurações separadas.
- `--server ws://host:9988/ws`: força o modo servidor.
- `--mesh-port 9987`
- `--nickname`
- `--autoconnect`
- `--debug`: habilita a fonte "Padrão de teste".

## Arquitetura (C++ ↔ TeamSpeak 6)

| TeamSpeak 6 (`TeamSpeak.dll`) | OpenSpeak nativo |
|---|---|
| comandos `setupstream`, `streamsignaling`… | `server/VirtualServer.*` + `server/ServerHost.*` (servidor dedicado) **ou** `client/mesh/MeshConnection.*` (malha na tailnet, sem servidor) |
| — | `client/mesh/PeerDirectory.*` (descoberta + identidade via Tailscale) |
| `ScreenshareManager` | `client/video/ScreenshareManager.*` |
| `StreamBase` / `StreamP2P` | `client/rtc/RtcSession.*` (libdatachannel) |
| `SessionOutbound` / `SessionInbound` | `client/rtc/SessionOutbound.*` / `SessionInbound.*` |
| `ScalableSharedVideoTransformer` (single-encoding) | `client/media/VideoEncoder.*`: **um** encoder H.264 para todos os espectadores |
| `DesktopCapturer` / `CameraCapturer` | `client/media/VideoSource.*` (QScreenCapture / QWindowCapture / QCamera) |
| `ProcessAudioCapturer` | `client/media/AudioCapture.*` (WASAPI *process loopback*) |
| NVENC / AMF / QSV / OpenH264 | mesma ordem de preferência via FFmpeg |
| estatísticas + detecção de problemas | `client/video/StreamStats.*` (limiares idênticos) + `client/rtc/RtpStatsHandler.*` |
| layout grade/foco | `client/video/Layout.*` |
| UI | `client/qml/*.qml` |

Detalhes do motor:
- **Single-encoding:** o encoder roda numa thread própria e cada quadro codificado é
  empacotado em RTP para cada espectador. Um PLI ou um espectador novo pede keyframe
  ao encoder compartilhado.
- **Controle de banda:** cada espectador tem uma estimativa AIMD baseada na perda dos
  Receiver Reports, limitada pelo REMB. O encoder segue o espectador mais limitado
  (NVENC troca o bitrate sem reiniciar).
- **Áudio:** tela inteira captura todo o áudio do sistema exceto o próprio OpenSpeak
  (evita eco); janela captura só o processo dono dela; câmera usa o microfone. Opus
  estéreo, 48 kHz.
- **Interop com navegador:** ofertas do Chrome são filtradas para H.264 + Opus (sem
  RTX/RED/FEC/transport-cc). A reconexão recria a PeerConnection nos dois lados.

## Testes

- `tst_virtualserver`: mesmos cenários do servidor JS.
- `tst_engine`: limiares de problemas, layout, filtro de SDP, ida e volta H.264
  (NVENC → FFmpeg) e Opus.
- `tst_windowthumbnail`: registro e liberação de previews DWM, inclusive quando o
  cartão é ocultado ou o diálogo fecha. O preview usa o HWND da fonte enumerada
  através de Qt Multimedia Private; essa integração deve ser revalidada ao atualizar o Qt.
- `ui_setup`, `ui_source`, `ui_view-menu`: carregamento QML e layout em 900×560,
  incluindo espaço da grade, largura dos controles de FPS e textos do menu.
- `tst_mesh_e2e`: dois nós sem servidor (diretório estático em loopback). Cobre
  descoberta, identidade verificada, pedido, vídeo, ban por identidade, troca de sala e
  saída da tailnet.
- `tst_p2p_e2e`: servidor + transmissor + espectador no mesmo processo, com P2P real
  via ICE local. Valida quadros 1280×720 decodificados, estatísticas, kick e fim do
  stream.

## Limitações conhecidas

- O app nativo negocia só **H.264** (o libdatachannel 0.24 não tem depacketizador AV1).
- **SFU** não implementado (o modo aparece desabilitado, como no TS sem `STREAM_SFU`).
- Sem sincronização fina áudio/vídeo (o TS tem `VideoAudioSyncBuffer`); os dois
  fluxos são de baixa latência independentes.
- Câmera e captura de áudio por janela ainda não foram testadas em uso real.
