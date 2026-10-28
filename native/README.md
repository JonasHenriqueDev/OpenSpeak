# OpenSpeak nativo (Qt 6 + libdatachannel + FFmpeg)

Aplicativo Windows (`OpenSpeak.exe`) e servidor (`openspeak-server.exe`) em C++.
A interface usa QML com tema próprio e seleção de fontes em grade. O motor de transmissão P2P
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
OpenSSL são linkados estaticamente. A compilação usa todos os núcleos
(`--parallel $env:NUMBER_OF_PROCESSORS` e `/MP` do MSVC).

A versão (`-Version`) entra só em `build\generated\Version.cpp`, gerado de
`common/Version.cpp.in` e exposto por `openspeak::version()` (`common/Version.h`).
Trocar de versão recompila apenas esse arquivo e refaz o link.

No dia a dia dá para rodar só parte dos testes (o build continua completo):

```bash
powershell -ExecutionPolicy Bypass -File native/build.ps1 -Tests 'tst_engine|ui_'   # regex passada ao ctest -R
powershell -ExecutionPolicy Bypass -File native/build.ps1 -Changed                  # só o afetado desde a última tag v*
```

O `-Changed` olha `git diff <última tag v*>..HEAD`, a working tree e os arquivos não
rastreados:

| Mudou | Testes |
|---|---|
| `client/qml/` | `ui_*` |
| `server/` | `tst_virtualserver`, `tst_sfu_e2e`, `tst_p2p_e2e` |
| `client/core`, `rtc`, `video`, `media` | `tst_engine`, `tst_p2p_e2e`, `tst_cloudconnection` |
| só `installer/`, `*.md` (CHANGELOG, docs) ou fora de `native/` | nenhum (pula o ctest) |
| qualquer outra coisa (CMake, `common/`, `tests/`…) | todos |

O `package.ps1` sempre roda a suíte completa antes de gerar a release.

Para reinstalar as dependências (≈30 min na primeira vez):

```bash
cd native && D:/Dev/vcpkg/vcpkg.exe install --triplet x64-windows-static-md --x-install-root=vcpkg_installed
```

## Instalar

Há duas edições, com o mesmo `OpenSpeak.exe`:

- **Lite** (padrão), `OpenSpeak-Setup-<versão>.exe`: cliente do servidor, com
  streams P2P direto ou pelo servidor (SFU).
- **Completa**, `OpenSpeak-Full-Setup-<versão>.exe`: inclui também o modo
  Tailnet, a sala web e o servidor dedicado (`openspeak-server.exe`).

A edição vem de `edition.txt` ao lado do `.exe` (sem o arquivo, o app roda como
completa). Em **Atualizações** dá para trocar de edição.

Baixe e rode o instalador. Ele já traz o Qt, o FFmpeg, o
WebRTC e o runtime do Visual C++, além de liberar o app no firewall. A conexão
via servidor não exige Tailscale. O [Tailscale](https://tailscale.com/download/windows)
é necessário somente para os modos avançados de tailnet e sala web.

Para gerar o instalador, rode o comando abaixo. Ele compila, testa e cria
`native/dist/OpenSpeak-Setup-<versão>.exe` (lite) e
`native/dist/OpenSpeak-Full-Setup-<versão>.exe` (completa).

```bash
powershell -ExecutionPolicy Bypass -File native/package.ps1
```

## Conexão via servidor (padrão)

Informe seu nome, o endereço do servidor (`wss://<host>/ws`) e, se ele for privado,
a senha na tela inicial. Endereço e senha ficam salvos depois do primeiro login.
A lista de participantes mostra o RTT de cada pessoa até o servidor,
e a barra superior mostra seu próprio ping. A medição é por ping/pong WebSocket,
com atualização a cada 15 segundos e uma primeira amostra ao conectar.

## Modo Tailnet (Avançado): sem servidor

Abra **Avançado** na tela inicial e clique em **Entrar via Tailscale**. Não há IP
nem servidor para configurar nesse modo.

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

### Sala web e servidor dedicado

Para usar Windows e navegador sem servidor em um PC, abra **Avançado** e escolha
**Entrar na sala web**. A entrada não pede código. A mídia continua na
tailnet, e o Vercel faz somente a sinalização. A opção Tailnet permanece
independente do Vercel. Veja [WEB-TAILSCALE.md](../WEB-TAILSCALE.md).

Na entrada principal, o app usa um servidor central, compatível também com o cliente web:

```bash
openspeak-server.exe --port 9988
```

O mesmo servidor também faz o **relay de mídia (SFU)**: no modo "Servidor" do diálogo
de transmissão, o transmissor envia **um** fluxo ao servidor, que repassa os pacotes
RTP a cada espectador sem transcodificar (veja [Servidor com SFU](#servidor-com-sfu)).

Opções do app:
- `--profile nome`: configurações separadas.
- `--server ws://host:9988/ws`: força o modo servidor.
- `--mesh-port 9987`
- `--nickname`
- `--autoconnect`
- `--debug`: habilita a fonte "Padrão de teste".

## Servidor com SFU

No modo P2P cada espectador custa um upload inteiro ao transmissor. No modo
**Servidor (SFU)** o transmissor manda um fluxo só ao `openspeak-server`, que o
repassa a cada espectador. O servidor é um par WebRTC como outro qualquer, com o
clid reservado `0`:

1. `setupstream` com `mode: 2` (só aceito quando o servidor anuncia `STREAM_SFU`).
2. O transmissor cria **uma** sessão de saída para o clid `0` e oferta; o servidor
   responde (`server/SfuIngest.*`).
3. Ao aceitar um espectador, o servidor responde com `peerClid: 0` e oferta a ele
   (`server/SfuEgress.*`); o espectador responde. O dono continua recebendo
   `notifystreamclientjoined` só para a lista de espectadores.
4. `server/SfuForwarder.*` repassa o RTP cru: troca PT/SSRC, remove header
   extensions, mantém seq/timestamp contínuos se o transmissor reconectar, pede
   keyframe (PLI, no máximo 1 a cada 500 ms) quando um espectador entra ou pede, e
   envia REMB ao transmissor com o mínimo das estimativas dos espectadores (piso de
   300 kbps, limitado ao bitrate do stream).

Escopo desta fase: codec fixo **H.264 (Constrained Baseline, packetization-mode=1) +
Opus**, uma qualidade só (sem simulcast), pensado para 1 transmissor e até ~4
espectadores. Privacidade, pedidos, limite, kick e ban funcionam como no P2P. O
servidor Node/Vercel continua só P2P (não anuncia `STREAM_SFU`).

Opções do `openspeak-server`:
- `--rtc-port 10000`: porta **UDP única** de todas as sessões do relay (ICE UDP mux).
  Libere-a no firewall (UDP). `0` usa portas efêmeras.
- `--public-ip <ip>`: IP anunciado no lugar do IP da interface nos candidatos do
  relay. Necessário com NAT 1:1 (VM na nuvem com IP privado `10.0.0.x` e IP público
  fixo); sem ele os clientes só recebem o `10.0.0.x` e não conectam.
- `--no-sfu`: só P2P.
- `OPENSPEAK_PASSWORD=<senha>`: servidor privado; o `login` sem a senha certa recebe
  `access_denied` e a conexão é derrubada. Vale para o app, a página web e a atividade
  do Discord. Fica em variável de ambiente para não aparecer na linha de comando.
- `OPENSPEAK_PASSWORD_FILE=<arquivo>`: a senha é relida desse arquivo (primeira linha)
  a cada login e, se ele existir, vale no lugar de `OPENSPEAK_PASSWORD`. O painel de
  [monitoramento](../monitor/README.md#senha-do-servidor) troca a senha nele, sem reiniciar.
  Quem já está conectado continua na sala.
- `OPENSPEAK_ICE='[{"urls":"stun:..."}]'`: STUN anunciado aos clientes e usado pelo
  próprio relay (padrão: `stun:stun.l.google.com:19302`).

### Linux (Ubuntu 24.04, x86_64) — só o servidor

O servidor não depende de FFmpeg nem de nada do Windows. O relay SFU precisa da
libdatachannel; sem ela o servidor compila só com P2P. Qt ≥ 6.8 é obrigatório (o
`qt6-*` do apt do Ubuntu 24.04 é 6.4, antigo demais para o `QHttpServer` usado), então
use o [aqtinstall](https://github.com/miurahr/aqtinstall):

```bash
sudo apt install build-essential cmake ninja-build git curl zip unzip tar pkg-config python3-pip libgl1-mesa-dev
pip install aqtinstall
aqt install-qt linux desktop 6.8.3 linux_gcc_64 -m qthttpserver qtwebsockets -O ~/Qt

git clone https://github.com/microsoft/vcpkg ~/vcpkg && ~/vcpkg/bootstrap-vcpkg.sh
# Só libdatachannel (o FFmpeg fica na feature "client", desligada aqui).
cmake -S native -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=$HOME/Qt/6.8.3/gcc_64 \
  -DCMAKE_TOOLCHAIN_FILE=$HOME/vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_MANIFEST_NO_DEFAULT_FEATURES=ON \
  -DOPENSPEAK_BUILD_CLIENT=OFF
cmake --build build
ctest --test-dir build --output-on-failure   # tst_virtualserver
```

Rodando numa VM (ex.: Oracle Cloud, IP privado `10.0.0.5`, público `203.0.113.10`):

```bash
./build/openspeak-server --port 9988 --rtc-port 10000 --public-ip 203.0.113.10 --no-web
```

Na VCN/security list e no firewall da VM, libere **TCP 9988** (sinalização) e
**UDP 10000** (mídia). No Ubuntu da Oracle as regras do `iptables` vêm restritivas:

```bash
sudo iptables -I INPUT -p tcp --dport 9988 -j ACCEPT
sudo iptables -I INPUT -p udp --dport 10000 -j ACCEPT
sudo netfilter-persistent save
```

## Arquitetura (C++ ↔ TeamSpeak 6)

| TeamSpeak 6 (`TeamSpeak.dll`) | OpenSpeak nativo |
|---|---|
| comandos `setupstream`, `streamsignaling`… | `server/VirtualServer.*` + `server/ServerHost.*` (servidor dedicado) **ou** `client/mesh/MeshConnection.*` (malha na tailnet, sem servidor) |
| — | `client/mesh/PeerDirectory.*` (descoberta + identidade via Tailscale) |
| `ScreenshareManager` | `client/video/ScreenshareManager.*` |
| `StreamBase` / `StreamP2P` | `common/rtc/RtcSession.*` (libdatachannel, comum ao cliente e ao relay) |
| SFU (`STREAM_SFU`) | `server/SfuRelay.*`, `SfuIngest.*`, `SfuEgress.*`, `SfuForwarder.*` (relay sem transcodificação) |
| `SessionOutbound` / `SessionInbound` | `client/rtc/SessionOutbound.*` / `SessionInbound.*` |
| `ScalableSharedVideoTransformer` (single-encoding) | `client/media/VideoEncoder.*`: **um** encoder H.264 para todos os espectadores |
| `DesktopCapturer` / `CameraCapturer` | `client/media/VideoSource.*` (QScreenCapture / QWindowCapture / QCamera) |
| `ProcessAudioCapturer` | `client/media/AudioCapture.*` (WASAPI *process loopback*) |
| NVENC / AMF / QSV / OpenH264 | mesma ordem de preferência via FFmpeg |
| estatísticas + detecção de problemas | `client/video/StreamStats.*` (limiares idênticos) + `common/rtc/RtpStatsHandler.*` |
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
- **Modo SFU:** o encoder segue a estimativa da sessão com o servidor, que por sua
  vez segue o REMB que o relay calcula sobre os espectadores.

## Testes

- `tst_virtualserver`: mesmos cenários do servidor JS, mais o modo SFU com um relay
  falso (capability, `peerClid: 0`, roteamento do clid 0 e permissões).
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
- `tst_sfu_e2e`: servidor com relay real (porta UDP única) + transmissor + dois
  espectadores. Valida que os dois decodificam 1280×720 a partir de um único envio,
  kick de um sem afetar o outro e fim do stream.

## Limitações conhecidas

- O app nativo negocia só **H.264** (o libdatachannel 0.24 não tem depacketizador AV1).
- **SFU** só no servidor nativo, sem simulcast/SVC nem TURN. O relay não pede
  retransmissão (NACK) ao transmissor: perdas no trecho transmissor → servidor só se
  recuperam com keyframe.
- Sem sincronização fina áudio/vídeo (o TS tem `VideoAudioSyncBuffer`); os dois
  fluxos são de baixa latência independentes.
- Câmera e captura de áudio por janela ainda não foram testadas em uso real.
