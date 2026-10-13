# OpenSpeak 0.2.6: Windows e navegador

Todos os computadores devem estar conectados à mesma tailnet, com tráfego entre eles permitido nas regras do Tailscale. Não há TURN nem STUN neste modo.

## Duas formas de entrar

**Tailnet, somente apps Windows:** a opção Entrar usa descoberta e sinalização diretas entre apps, na porta 9987 da interface Tailscale. Funciona sem Vercel, Redis ou servidor dedicado. Cada app controla seus próprios streams. Fechar um app encerra seus streams; os demais continuam conectados entre si.

**Sala web, Windows e navegadores:** abra https://openspeak-dun.vercel.app/ e informe o apelido. O navegador tenta detectar o IP Tailscale; se não conseguir, informe o IP do próprio PC. No Windows, selecione **Sala web • Windows e navegadores** e entre sem código. Todos usam a sala comum no Vercel; nenhum Windows precisa hospedar um servidor. Dois navegadores podem negociar entre si mesmo sem apps Windows abertos.

Os dois modos possuem listas de participantes independentes. Para encontrar alguém no navegador, entre em Sala web no Windows, em vez de Entrar na tailnet. A sala web é pública: quem acessar o link pode ver participantes e miniaturas. O IP informado não autentica a pessoa nem confirma que o Tailscale está conectado. Tokens temporários individuais continuam protegendo a retomada de cada sessão.

## Caminho dos dados

Vercel recebe comandos, presença, miniaturas públicas e mensagens SDP/ICE. Redis guarda o estado temporário da sala e distribui notificações entre instâncias. O vídeo e o áudio usam WebRTC entre participantes pela tailnet. Se o Tailscale usar DERP, a mídia passa pelo relay criptografado do Tailscale. Vercel e Redis não recebem o fluxo de vídeo/áudio.

O navegador mascara IPs locais com mDNS. Como esses nomes não resolvem entre máquinas da tailnet, o cliente anuncia o endereço Tailscale informado no login para seus candidatos host mascarados, preservando portas e credenciais ICE. IPs de LAN, candidatos públicos e TURN não são anunciados nesse modo. O endereço precisa pertencer ao PC que está executando o navegador.

## Reconexão e fechamento

As funções do Vercel têm duração máxima de 300 segundos no Hobby. Os clientes reconectam automaticamente e retomam o mesmo participante, sem recriar as conexões de mídia ativas. A retomada tem tolerância de 90 segundos. Depois disso, a sessão entra novamente e os streams antigos são removidos: uma transmissão encerrada não é recriada sem uma nova ação do usuário.

Ao desconectar normalmente, o cliente envia logout. Em quedas abruptas, os outros clientes removem a presença expirada durante a próxima atualização. Não há serviço OpenSpeak, tarefa agendada ou processo de servidor iniciado automaticamente. O Tailscale continua sendo um aplicativo separado.

## Publicar

Instale as dependências com npm ci. Vincule o projeto do Vercel na raiz do repositório e configure REDIS_URL (integração Upstash) como segredo de produção. Não existe código de sala desde a versão 0.2.4. Publique a raiz, não a antiga pasta web-dist:

```powershell
npx vercel deploy --prod --scope jonashenriquedevs-projects
```

scripts/build-cloud.mjs configura o cliente hospedado para sinalização cloud. api/ws.js usa WebSockets e Redis com commit atômico, sem estado compartilhado apenas em memória. Uma nova sessão invalida o socket anterior. A sala tem limite de 32 participantes e cada socket aceita até 180 comandos por minuto.

Vercel Hobby e Upstash Free foram selecionados. Gratuidade depende das cotas, incluindo os 500 mil comandos mensais do Redis. Não há contratação de plano pago. WebSockets no Vercel estão em beta: https://vercel.com/docs/functions/websockets

## Testes e release

npm test verifica protocolo, estado distribuído, sessões, retomada e candidatos. native/build.ps1 compila e testa malha, mídia, interface e retomada do transporte nativo. native/package.ps1 gera instalador, ZIP e portátil de um único executável, com as mesmas funções do app instalado.

O portátil extrai dependências numa pasta temporária própria e remove essa pasta ao sair. --local-test abre dois clientes e um servidor de teste temporário em loopback; ele termina ao fechar ambos. Não é necessário para a sala web.

Um teste no mesmo PC verifica interoperabilidade e reprodução, mas não substitui um teste em dois PCs diferentes para confirmar regras de tailnet e CGNAT/DERP.

### Estado da validação desta pré-release

Passaram 32 testes JavaScript e 9 suítes nativas, incluindo mídia P2P e malha Windows em loopback. O portátil de um único exe também passou no teste de abertura sem Qt instalado no PATH.

Windows para navegador e navegador para navegador reproduziram vídeo em 1280x720 no mesmo PC. No teste entre navegador Windows e navegador Linux, a sala e as miniaturas funcionaram, mas o vídeo permaneceu conectando. Essa rota entre PCs ainda precisa de correção e não deve ser considerada validada nesta versão. O anúncio de candidatos mDNS pelo IP informado é experimental.

Para testar Windows com Windows sem Vercel, use **Entrar** na tailnet nos dois apps. Não selecione Sala web para esse teste.
