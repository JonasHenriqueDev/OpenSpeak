# OpenSpeak 1.0.1 — conexão via servidor e ping

- Conexão via servidor é a opção principal na tela inicial. Tailscale e sala web
  ficam recolhidos dentro de **Avançado**, e só aparecem ao expandir.
- O ping de cada pessoa até o servidor aparece na lista de participantes no web
  e no Windows. Seu ping até o servidor também aparece na barra superior.
  Medição de RTT via ping/pong WebSocket; não é latência de mídia entre usuários.
- Botão de atualizações na barra da janela usa o ícone de download.

# OpenSpeak 1.0.0 — release estável

- Atualizações do app instalado pelas releases do GitHub: canal estável por padrão,
  com opção **Receber pré-releases**. Download automático em segundo plano,
  validação SHA-256 e instalação ao fechar o app. Não interrompe transmissões.
- Botão de atualizações na barra superior com versão, estado, verificação manual
  e aplicação com reinício. Configurações e identidade são preservadas.
- Link discreto no cliente web para o instalador da última release estável.
- Identificação de versão/sistema do app nativo no monitor, métricas individuais
  do SFU e correções das estatísticas SFU/P2P e dos controles em tela cheia no web.

# OpenSpeak nativo 0.2.7

- Modo **Servidor (SFU)**: o transmissor envia um fluxo só ao `openspeak-server`,
  que repassa o RTP (H.264 + Opus) a cada espectador sem transcodificar. O upload do
  transmissor deixa de crescer com o número de espectadores. Disponível quando o
  servidor anuncia `STREAM_SFU`; a malha da tailnet e a sala web continuam P2P.
- `openspeak-server`: `--rtc-port` (porta UDP única do relay, padrão 10000),
  `--public-ip` (IP anunciado atrás de NAT 1:1) e `--no-sfu`. Compila também em Linux
  (só o servidor, sem FFmpeg).
- Corrige a entrada em servidor dedicado: o endereço digitado era trocado pelo salvo
  (`ws://localhost:9988/ws`) ao clicar em Conectar.

# OpenSpeak nativo 0.2.6

- Opção "Sem áudio do Discord" na transmissão de tela: o áudio do app do Discord
  (estável, PTB ou Canary, com todos os processos filhos) fica fora da transmissão.
  O Discord é detectado a cada segundo; se abrir, fechar ou reiniciar, a captura é
  recriada sozinha. O Windows só exclui uma árvore de processos, então enquanto a opção
  estiver ativa as transmissões assistidas ficam sem som, para não vazarem para a sua.
  As estatísticas de áudio mostram o que está sendo capturado.

# OpenSpeak nativo 0.2.5

- Captura de janela sem a borda amarela do Windows.Graphics.Capture (Windows 10 não
  permite desligá-la). Janela visível e sem nada por cima: duplicação da tela (DXGI)
  recortada na GPU, no monitor com a maior parte da janela. Janela coberta:
  `PrintWindow(PW_RENDERFULLCONTENT)`. Preto nos dois modos por ~1 s volta para o
  `QWindowCapture` (com borda). Captura no fps alvo, cursor desenhado, janela
  minimizada reenvia o último quadro. `OPENSPEAK_WINDOW_CAPTURE=wgc` força o método antigo.
- As estatísticas da transmissão mostram o método de captura em uso.
- Licença AGPL-3.0-or-later; licenças das dependências vão em `licenses/` no pacote.

# OpenSpeak nativo 0.2.2

- Filtro de janelas baseado no HWND real: remove superfícies sem título, ocultas,
  cloaked, tool windows, popups internos e barras de tarefas. Janelas reais de
  Configurações continuam disponíveis.
- Miniaturas DWM seguem imediatamente a geometria dos ancestrais e as animações,
  acompanhando a rolagem com recorte no viewport. Teste verifica pixels no desktop.
- Portátil em um único executável, com ZIP embutido, extração automática no Temp
  e limpeza ao fechar. O launcher tem runtime estático e usa PowerShell do Windows
  para extrair; o app interno é o mesmo do instalador. Configurações ficam no perfil.
- `--local-test` no portátil inicia um servidor e dois perfis separados no mesmo PC.
- `--server-only` permite hospedar a sinalização privada com o mesmo portátil.
  O servidor aceita `--host` para escutar só no loopback atrás do Tailscale Serve.
- Cliente web publicado como entrada no Vercel: a sessão abre na origem HTTPS da
  tailnet, evitando acesso entre uma origem pública e a rede privada. Tailscale é
  obrigatório. Sem TURN nesta versão. Apps nativos conectados a servidores ts.net
  vinculam a mídia ao endereço do Tailscale, sem servidores STUN/TURN.

# OpenSpeak nativo 0.2.1

- Seleção de telas, janelas e câmeras em uma grade de cartões com miniaturas.
  Previews de janelas usam o compositor do Windows (DWM), inspirado apenas no preview
  do TailCast. Telas usam snapshots; câmeras e o fallback usam Qt Multimedia.
  Snapshots são atualizados ao abrir ou atualizar a lista; a fonte selecionada mostra
  vídeo ao vivo. Capturas indisponíveis recebem um aviso no cartão.
- Trocar fonte usa a mesma grade e preserva a transmissão e os espectadores.
  O preview local e a tela cheia acompanham a nova fonte.
  A seleção mantém a janela enumerada, mesmo se a ordem das janelas mudar.
  Uma janela fechada ou câmera desconectada não seleciona outra fonte por engano.
- Barra de título integrada ao tema, com controles de minimizar, maximizar/restaurar,
  fechar, arrastar e redimensionar. Ícone existente do OpenSpeak incluído no app,
  executável, atalhos e instalador.
- Rodapé do diálogo com as cores da interface; botões Cancelar e Ficar ao vivo
  alinhados à direita. Configurações com rolagem e controles de FPS ajustados à largura.
- Menu Exibição dimensionado para mostrar os textos completos.
- Testes para miniaturas, cancelamento da captura, fontes que desapareceram,
  troca de fonte com espectador conectado e layout QML em 900×560.

Artefatos gerados por `package.ps1`: instalador e ZIP portátil.
