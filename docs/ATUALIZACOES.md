# Downloads e atualizações

O cliente web oferece **Download App** na tela de entrada e na barra superior
quando conectado. Os links apontam para:

`https://github.com/JonasHenriqueDev/OpenSpeak/releases/latest/download/OpenSpeak-Setup.exe`

O GitHub direciona esse endereço à última release estável. Pré-releases e drafts
não são oferecidos pelo link web.

## App instalado

Ao abrir, o app consulta o GitHub antes de liberar a conexão. Quando encontra
uma versão mais nova no canal escolhido, baixa e valida o instalador, instala e
reabre automaticamente, sem perguntar. A tela inicial mostra o estado da
verificação e do download. Se já está atualizado ou a consulta/download falha,
a entrada é liberada normalmente.

No botão com ícone de download na barra da janela:

- **Verificar também a cada 6 horas**: habilitado por padrão. Controla somente
  as consultas periódicas; a verificação ao abrir ocorre sempre.
- **Receber pré-releases**: desabilitado por padrão. Desligado recebe somente
  releases estáveis. Ligado recebe a versão mais nova entre releases e pré-releases.
- **Edição**: não há mais escolha na janela. Cada instalação continua na própria
  edição: **Lite** (padrão, só o cliente do servidor, com streams P2P ou pelo
  servidor/SFU) ou **Completa** (inclui também Tailscale, sala web e servidor
  dedicado). Para trocar, instale o instalador da outra edição; as duas usam a
  mesma pasta e as mesmas configurações.
- **Verificar agora**: consulta manual. Atualizações encontradas durante o uso
  ficam para a próxima abertura, sem interromper conexões ou transmissões.
- **Baixar atualização**: apenas baixa e valida. Para antecipar uma atualização
  durante o uso, escolha **Aplicar e reiniciar**.
- **Aplicar e reiniciar**: pede confirmação antes de encerrar conexões existentes.

O instalador usa `%LOCALAPPDATA%\Programs\OpenSpeak`, somente para o usuário
atual, e não solicita elevação pelo UAC. Não modifica regras globais do firewall.
Os modos avançados que recebem conexões podem precisar de autorização no firewall
do Windows, separadamente da instalação.
Não reinicia o Windows nem força o fechamento de outras instâncias. Configurações
e identidade continuam no QSettings do usuário. Ao migrar de Program Files, os
arquivos antigos não são removidos automaticamente; a desinstalação da versão
antiga, se desejada, continua sendo uma operação administrativa. O SHA-256 anunciado pelo GitHub
é conferido ao baixar e antes de executar o instalador. Falhas de rede ou de
integridade não executam o instalador; o app continua utilizável.

A versão portátil não executa atualização por instalador: mostra a disponibilidade
e a página de downloads. As versões anteriores a 1.0.0 precisam da instalação
manual da 1.0.0 para receber o mecanismo automático.

## Publicar versões futuras

1. Atualize as versões do projeto e compile/teste antes da publicação.
2. Execute `native/package.ps1 -Version <versão>`. O parâmetro é repassado ao
   build para que a versão informada pelo app corresponda ao instalador.
3. Publique na release os instaladores das duas edições, cada um com o nome
   versionado e o fixo: `OpenSpeak-Setup-<versão>.exe` e **`OpenSpeak-Setup.exe`**
   (lite, padrão) e `OpenSpeak-Full-Setup-<versão>.exe` e
   **`OpenSpeak-Full-Setup.exe`** (completa). O nome fixo mantém o link web
   funcionando em todas as releases estáveis. O app de cada edição só aceita
   o instalador da edição escolhida.
4. Releases de produção usam tags como `v1.0.1`, com pré-release desmarcado.
   Versões de teste usam tags como `v1.1.0-beta.1` e pré-release marcado.
5. Use `native/package.ps1 -Version 1.1.0-beta.1` para que os clientes desse
   canal possam migrar para `1.1.0` estável depois. Não reutilize assets de outra versão.

O updater ignora drafts, versões inválidas, instaladores sem digest SHA-256 e URLs
fora do repositório oficial. Usa a versão semântica, não a ordem lexical das tags,
e não faz downgrade quando o usuário desabilita pré-releases.
