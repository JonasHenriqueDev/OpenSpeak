# Monitoramento da VM Oracle

Painel em `https://openspeak.duckdns.org/monitor/`, protegido por HTTP Basic Auth no Caddy.
O usuário do painel é `admin`; a senha é fornecida separadamente e não fica no repositório.
Além da leitura, o painel só troca a senha do servidor (sem ações de moderação).

O servidor nativo escreve um snapshot local atômico a cada 2 segundos quando
`OPENSPEAK_METRICS_FILE` está definido. O snapshot inclui somente participantes
autenticados, canais, streams, espectadores, IPs, User-Agent, dados de conexão,
contadores de sinalização e métricas das sessões SFU. Não inclui UID, miniaturas
nem conteúdo de mídia. Estes campos ficam somente no snapshot local e no painel
autenticado; não são acrescentados à lista pública da sala.
Não cria participantes artificiais na sala.

`monitor.py` usa apenas a biblioteca padrão do Python 3. Coleta recursos Linux e
bytes das interfaces de rede físicas a cada 5 segundos. Escuta em `127.0.0.1:9990`;
Caddy encaminha `/monitor` e `/monitor/*` após autenticar. Não abra essa porta no
firewall. O acesso direto ao OpenSpeak na porta 9988 não expõe o monitoramento.

## Dados

- Pessoas: sessões com login no OpenSpeak; uma pessoa com dois clientes conta duas vezes.
- Espectadores: inscrições em streams, não confirmações de reprodução de mídia.
- Tráfego: toda a VM, incluindo SFU e outros serviços. Mídia P2P entre usuários é externa à VM.
- Sinalização: bytes JSON desde o início do processo OpenSpeak, sem overhead WebSocket/TLS.
- Participantes: transmissões que publica e assiste, IP de sinalização e endpoint ICE da mídia quando SFU.
- IP: o cabeçalho `X-Forwarded-For` só é aceito do proxy em loopback; conexões diretas usam o IP do socket.
- Cliente: navegador/sistema pelo User-Agent; apps atualizados informam nome, versão e plataforma no login. Apps anteriores sem User-Agent podem aparecer como não identificados. A identificação enviada pelo cliente não é autenticação.
- Banda por usuário: taxas calculadas por deltas de contadores RTP do SFU, separadas em envio do usuário (ingest) e recebimento do usuário (egress). Não inclui overhead de rede, RTCP nem retransmissões por NACK; por isso não equivale ao tráfego total da interface da VM.
- Totais de mídia por usuário: sessões SFU que ainda estão abertas; encerrar a sessão remove seus contadores da linha.
- P2P: inscrições são visíveis; banda de mídia, endpoint e qualidade não são observáveis pelo servidor.
- RTT de sinalização: tempo completo de ida e volta do ping WebSocket. RTT/perda/jitter de vídeo para espectadores SFU quando há Receiver Reports; métricas ausentes ficam como não disponíveis.
- Rede acumulada: observada somente enquanto o monitor está coletando; não é a medição de cobrança da Oracle.
- Histórico: 7 dias de amostras, totais diários persistentes em `/var/lib/openspeak-monitor/metrics.sqlite`.
- Dias e meses: fuso `America/Sao_Paulo`. Sem dados retroativos anteriores à instalação.

## Instalação

Compile o servidor nativo com as alterações de monitoramento e substitua o binário
Linux em `/opt/openspeak-server/openspeak-server` mantendo as bibliotecas existentes.
Guarde um backup antes da substituição.

Copie esta pasta para `/opt/openspeak-monitor`, o serviço para
`/etc/systemd/system/openspeak-monitor.service`, e `openspeak-metrics.conf` para
`/etc/systemd/system/openspeak.service.d/monitor.conf`.

No bloco HTTPS existente do Caddy, adicione (Caddy 2.6 usa `basicauth`):

```caddy
@monitor path /monitor /monitor/*
handle @monitor {
    basicauth {
        admin <HASH_BCRYPT>
    }
    reverse_proxy 127.0.0.1:9990
}
handle {
    reverse_proxy 127.0.0.1:9988
}
```

Gere o hash com `caddy hash-password`, valide a configuração antes de recarregar e
ative os serviços. O primeiro reinício do OpenSpeak desconecta as sessões atuais.

```sh
sudo caddy validate --config /etc/caddy/Caddyfile
sudo systemctl daemon-reload
sudo systemctl restart openspeak
sudo systemctl enable --now openspeak-monitor
sudo systemctl reload caddy
```

Para trocar a senha, gere outro hash bcrypt, substitua o hash no Caddyfile,
valide e recarregue o Caddy. A senha do painel é independente do admin cloud/Vercel.
HTTP Basic Auth usa o diálogo de credenciais do navegador e pode permanecer em
cache até fechar a sessão do navegador.

## Senha do servidor

O painel **Senha do servidor** mostra e troca a senha exigida no login. O monitor
grava a senha em `OPENSPEAK_PASSWORD_FILE` (padrão
`/var/lib/openspeak-monitor/server-password`, `0640`) de forma atômica, e o
`openspeak-server`, com a mesma variável no drop-in `monitor.conf`, relê o arquivo
a cada login: a troca vale já no próximo login, sem reiniciar, e quem está
conectado continua na sala. A senha tem de 4 a 128 caracteres. Enquanto o arquivo
não existir, vale `OPENSPEAK_PASSWORD` do serviço (ou nenhuma). O servidor precisa
ler esse arquivo: rode os dois serviços com o mesmo usuário ou grupo `openspeak`.
A troca exige o cabeçalho `X-OpenSpeak-Monitor` e a mesma origem, para que outro
site não aproveite o Basic Auth em cache.

## Alertas

O monitor envia notificações pelo [ntfy](https://ntfy.sh) quando `NTFY_TOPIC` está
definido em `/etc/openspeak-monitor.env` (fora do repositório, `chmod 640`, grupo `openspeak`):

```sh
NTFY_TOPIC=<tópico-secreto>
# Opcionais: NTFY_SERVER=https://ntfy.sh  MONITOR_MONTHLY_EGRESS_TB=10  MONITOR_LINK_MBPS=50
```

O nome do tópico funciona como senha: quem o conhece lê e publica nele.

| Alerta | Condição |
|---|---|
| OpenSpeak fora do ar | snapshot parado ou processo ausente por 30 s |
| RAM alta | acima de 90% por 5 min |
| CPU alta | acima de 90% por 10 min |
| Disco quase cheio | acima de 85% |
| Banda de saída | acima de 80% de `MONITOR_LINK_MBPS` por 2 min |
| Tráfego mensal | projeção do mês acima de 80% de `MONITOR_MONTHLY_EGRESS_TB` (após 1 dia de dados no mês) |

Cada alerta repete no máximo uma vez por hora enquanto persistir e envia "Normalizado"
quando a condição some. Ao iniciar, o monitor envia "Monitor iniciado" (indica reinício
da VM ou do serviço). Alertas internos não detectam a VM inteira fora do ar; para isso
use um verificador externo (UptimeRobot) em `https://openspeak.duckdns.org/`.

Testes: `python3 monitor/test_monitor.py`, `npm test` e `ctest` do servidor nativo.
O teste nativo `tst_sfu_e2e` valida mídia para dois espectadores, os contadores
RTP por sessão, endpoints, identificação do cliente e participantes no snapshot.
