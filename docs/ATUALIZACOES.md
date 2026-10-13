# Downloads e atualizações

O cliente web oferece **Download App** na tela de entrada e na barra superior
quando conectado. Os links apontam para:

`https://github.com/JonasHenriqueDev/OpenSpeak/releases/latest/download/OpenSpeak-Setup.exe`

O GitHub direciona esse endereço à última release estável. Pré-releases e drafts
não são oferecidos pelo link web.

## App instalado

No botão de configurações de atualizações na barra da janela:

- **Atualizações automáticas**: habilitado por padrão. Consulta ao abrir (15 s)
  e a cada 6 horas; baixa o instalador em segundo plano e aplica ao fechar o app.
- **Receber pré-releases**: desabilitado por padrão. Desligado recebe somente
  releases estáveis. Ligado recebe a versão mais nova entre releases e pré-releases.
- **Verificar agora**: consulta manual. Com atualização automática desligada,
  o download e a instalação também são manuais.
- **Aplicar e reiniciar**: pede confirmação antes de encerrar conexões existentes.

O instalador pode pedir elevação pelo UAC porque a instalação usa Program Files.
Não reinicia o Windows nem força o fechamento de outras instâncias. Configurações
e identidade continuam no QSettings do usuário. O SHA-256 anunciado pelo GitHub
é conferido ao baixar e antes de executar o instalador. Falhas de rede ou de
integridade não executam o instalador; o app continua utilizável.

A versão portátil não executa atualização por instalador: mostra a disponibilidade
e a página de downloads. As versões anteriores a 1.0.0 precisam da instalação
manual da 1.0.0 para receber o mecanismo automático.

## Publicar versões futuras

1. Atualize as versões do projeto e compile/teste antes da publicação.
2. Execute `native/package.ps1 -Version <versão>`. O parâmetro é repassado ao
   build para que a versão informada pelo app corresponda ao instalador.
3. Publique na release os dois nomes do mesmo instalador:
   `OpenSpeak-Setup-<versão>.exe` e **`OpenSpeak-Setup.exe`**. O nome fixo mantém
   o link web funcionando em todas as releases estáveis.
4. Releases de produção usam tags como `v1.0.1`, com pré-release desmarcado.
   Versões de teste usam tags como `v1.1.0-beta.1` e pré-release marcado.
5. Use `native/package.ps1 -Version 1.1.0-beta.1` para que os clientes desse
   canal possam migrar para `1.1.0` estável depois. Não reutilize assets de outra versão.

O updater ignora drafts, versões inválidas, instaladores sem digest SHA-256 e URLs
fora do repositório oficial. Usa a versão semântica, não a ordem lexical das tags,
e não faz downgrade quando o usuário desabilita pré-releases.
