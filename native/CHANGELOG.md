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
