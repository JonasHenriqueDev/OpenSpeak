# OpenSpeak 1.0.14 — quedas de internet pausam em vez de desconectar

- Se a sua internet cair ou parar de responder por alguns segundos, você
  **não sai mais da sala**: o app reconecta sozinho ("Reconectando…") e volta
  de onde parou, como no Discord.
- A sua transmissão **não acaba**: para quem assiste ela aparece **pausada**
  enquanto você está fora e volta sozinha quando a conexão retorna.
- O app percebe uma conexão morta em segundos, em vez de ficar travado
  esperando.
- Se a queda passar de 90 segundos, você sai da sala como antes.

# OpenSpeak 1.0.13 — transmissão de tela sem o áudio do Discord por padrão

- A opção **"Sem áudio do Discord"** agora vem **ligada por padrão** ao
  transmitir a tela: vozes e sons do Discord não vão para quem assiste.
  Quem já tinha desligado a opção antes continua com a escolha salva.
- Enquanto ela estiver ativa, as transmissões que você assiste ficam sem som
  durante a sua transmissão, para o áudio delas não vazar para a sua.
  Desligue a opção na tela de transmissão se quiser ouvir.

# OpenSpeak 1.0.12 — imagem sem pixels corrompidos quando a rede oscila

- Quando um pacote de vídeo se perde no caminho, o app agora **pede de novo
  só o pedaço que faltou**. Na maioria das vezes ele chega a tempo e você nem
  percebe a perda.
- Se o pedaço não chegar a tempo, o quadro quebrado **não é mostrado**: a
  imagem segura o último quadro bom por um instante e volta limpa no próximo
  quadro completo, em vez de mostrar blocos de pixels fora do lugar.
- No modo **Servidor** (SFU), o servidor também pede de volta o que se perdeu
  entre quem transmite e o servidor.

# OpenSpeak 1.0.11 — bot do Discord e janela mais fácil de usar

- **Bot do Discord**: o servidor pode avisar no Discord quem está ao vivo e
  qual app está transmitindo (status da call, mensagem num canal e status do
  bot), na hora em que a transmissão começa.
- Nova opção **"Mostrar ao vivo no bot do Discord"** nas configurações
  avançadas da transmissão (ligada por padrão). Desligue para a transmissão
  não aparecer no bot.
- Ao transmitir uma **janela**, o app informa o nome do programa (por exemplo
  "Counter-Strike 2" ou "Google Chrome"), que aparece no aviso do bot.
- Clicar no ícone do OpenSpeak na **barra de tarefas** agora minimiza o app.
- A janela de **picture-in-picture** pode ser redimensionada por qualquer
  borda ou canto, não só pelo canto inferior direito.

# OpenSpeak 1.0.10 — verificação de atualizações mais confiável

- Quando o GitHub limita as consultas da sua rede (60 por hora por IP), o app
  não mostra mais "Não foi possível verificar atualizações": ele consulta a
  página de releases e confere o instalador pelo arquivo `SHA256SUMS` publicado.
- Versões de teste e perfis de depuração não consultam mais o GitHub sozinhos,
  para não gastar o limite dividido com o app instalado.

# OpenSpeak 1.0.9 — quem assiste junto e novidades

- Quem está **assistindo** a uma transmissão também ouve o som quando alguém
  começa ou para de assistir, inclusive ao entrar e ao sair você mesmo.
- Passe o mouse sobre a contagem de espectadores de uma transmissão que você
  está assistindo para ver **quem está assistindo junto**.
- **Novidades** nas Preferências: veja o que mudou em cada versão.
- Depois de atualizar, o app avisa "OpenSpeak atualizado para a versão …" com
  um atalho para as novidades.
- Ícone de engrenagem das Preferências redesenhado (não fica mais achatado).

# OpenSpeak 1.0.8 — transmissão pelo servidor por padrão e quem está assistindo

- Novas transmissões começam no modo **Servidor** (SFU): o servidor repassa o
  vídeo para os espectadores, e o seu upload não cresce com cada pessoa assistindo.
- Em servidores sem relay de mídia (servidor web, tailnet e sala web) a
  transmissão cai sozinha para **P2P**, sem erro.
- Ainda dá para escolher **P2P** no diálogo de transmissão, e a escolha é lembrada.
- Passe o mouse sobre a contagem de espectadores da **sua** transmissão (na
  árvore de canais ou no quadro do stream) para ver **quem está assistindo**.
- Novo efeito sonoro quando alguém começa ou para de assistir à sua
  transmissão, nos estilos Suave e Bolha (mais discreto que os sons do canal).
- O selo ao lado do nome do servidor mostra **SFU** quando o servidor repassa as
  transmissões (antes mostrava sempre "P2P", mesmo com SFU disponível).
- Ícone de configurações agora é uma engrenagem.

# OpenSpeak 1.0.7 — efeitos sonoros e controles em tela cheia

- Efeitos sonoros ao entrar e sair do canal, quando alguém entra ou sai do seu
  canal e quando uma transmissão do canal começa, é pausada ou encerrada.
- Dois estilos: **Suave** (padrão) e **Bolha**. Escolha em **Preferências › Sons**,
  com prévia ao trocar e controle de volume.
- Para silenciar, desmarque **Tocar efeitos sonoros** nas Preferências ou use
  **Silenciar efeitos sonoros** no menu do ícone da bandeja.
- O cliente web ganhou os mesmos sons e opções em **Configurações**.
- Tela cheia com controles: ao mexer o mouse aparecem o nome do stream e os
  botões de pausar/retomar, parar, volume, sair do stream, picture-in-picture e
  sair da tela cheia. Eles somem, junto com o cursor, após 2,5 s parado.

# OpenSpeak 1.0.6 — bandeja do sistema

- Ícone na bandeja do Windows, como no Discord: o X esconde a janela e o
  OpenSpeak continua rodando, com transmissões e streams assistidos ativos.
- Clique no ícone para reabrir; com o botão direito: abrir, parar transmissão,
  silenciar streams assistidos, preferências e **Sair do OpenSpeak**.
- Nova tela de **Preferências**: minimizar para a bandeja ao fechar (ligado por
  padrão), iniciar com o Windows e iniciar minimizado na bandeja.
- Abrir o app de novo traz de volta a janela que está na bandeja, em vez de
  abrir uma segunda cópia.
- O picture-in-picture continua na tela quando o app é minimizado.

# OpenSpeak 1.0.5 — prévia de janelas minimizadas

- No seletor de fonte, janelas minimizadas mostram o último quadro, como no
  Alt+Tab, em vez de uma miniatura vazia ou só com a barra de título.

# OpenSpeak 1.0.4 — atualização visível

- A instalação automática mostra a barra de progresso em vez de rodar invisível.
  Antes, o app fechava e nada aparecia por uns 15 segundos até reabrir.
- Abrir o app durante a instalação mostra um aviso de que ele abrirá sozinho,
  em vez de fechar de novo ou disparar outra instalação.

# OpenSpeak 1.0.3 — edição lite e picture-in-picture

- Nova edição **Lite**, agora a padrão (`OpenSpeak-Setup.exe`): somente o cliente
  do servidor, com streams P2P direto ou pelo servidor (SFU). Não traz Tailscale,
  sala web nem servidor dedicado.
- A edição **Completa** continua disponível (`OpenSpeak-Full-Setup.exe`). Em
  Atualizações, escolha **Lite** ou **Completa**; a troca é instalada como
  atualização e preserva as configurações.
- Botão **Picture-in-picture** na barra de cada stream: abre uma janela pequena,
  sem borda, arrastável e redimensionável, no canto inferior direito da tela.
- Fixada por padrão por cima de todos os apps; o botão de alfinete alterna e a
  escolha fica salva.
- Controles ao passar o mouse: pausar/parar no seu stream, volume/sair nos outros.
  Duplo clique ou Esc voltam para a janela do app.

# OpenSpeak 1.0.2 — atualização automática ao abrir

- Verifica o canal selecionado em toda abertura, antes de conectar. Uma versão
  nova é baixada, validada pelo SHA-256 e instalada com reinício automático.
- Tela inicial mostra o estado da verificação e do download, sem pedir confirmação.
- Instalador por usuário em AppData, sem exigir administrador nas atualizações.
- Falhas na consulta ou no download liberam a entrada normalmente.
- A opção de verificação periódica controla somente as consultas a cada 6 horas.
  Atualizações encontradas durante o uso ficam para a próxima abertura.

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
