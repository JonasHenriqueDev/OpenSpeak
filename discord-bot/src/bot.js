// Bot do Discord do OpenSpeak: consulta o GET /api/live do servidor e avisa quem
// está transmitindo (e qual app) no status do canal de voz, num canal de texto e
// no status do próprio bot. Cada aviso liga/desliga na config; quem transmite
// escolhe por stream ("Mostrar ao vivo no bot do Discord", configurações avançadas).
//
// Uso: DISCORD_TOKEN=... node src/bot.js   (config em ./config.json ou OPENSPEAK_BOT_CONFIG)

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import {
  ActivityType, ChannelType, Client, EmbedBuilder, GatewayIntentBits, InteractionContextType, MessageFlags,
  PermissionFlagsBits, Routes,
  SlashCommandBuilder,
} from 'discord.js';
import {
  NOTICES, appLabel, applySettings, checkConfig, diff, formatDuration, parseLive, parseStats, planVoiceUpdates,
  mediaText, presenceText, serverText, statsVars, streamKey, voiceStatusText, watchersText,
} from './live.js';
import { startWebhook } from './webhook.js';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const CONFIG_FILE = path.resolve(process.env.OPENSPEAK_BOT_CONFIG ?? path.join(ROOT, 'config.json'));
const STATE_FILE = path.resolve(process.env.OPENSPEAK_BOT_STATE ?? path.join(ROOT, 'state.json'));
/** Falhas seguidas ao consultar o servidor até considerar que ninguém está ao vivo. */
const OFFLINE_AFTER_FAILURES = 4;
/** Intervalo mínimo entre trocas do status do bot (limite do Discord). */
const PRESENCE_MIN_MS = 5000;

const log = (...args) => console.log(new Date().toISOString(), ...args);

function loadConfig() {
  let config;
  try {
    config = JSON.parse(fs.readFileSync(CONFIG_FILE, 'utf8'));
  } catch (err) {
    console.error(`Não foi possível ler ${CONFIG_FILE}: ${err.message}\nCopie config.example.json para config.json e preencha.`);
    process.exit(1);
  }
  const problems = checkConfig(config);
  const token = process.env.DISCORD_TOKEN || config.token;
  if (!token) problems.push('defina DISCORD_TOKEN (variável de ambiente) ou "token" na config');
  if (problems.length) {
    console.error(`Config inválida (${CONFIG_FILE}):\n - ${problems.join('\n - ')}`);
    process.exit(1);
  }
  return { config, token };
}

/** Mensagens publicadas no chat, por stream: sobrevivem a reinícios do bot. */
function loadState() {
  try {
    const state = JSON.parse(fs.readFileSync(STATE_FILE, 'utf8'));
    return {
      messages: state.messages && typeof state.messages === 'object' ? state.messages : {},
      voice: state.voice && typeof state.voice === 'object' ? state.voice : {}, // canal -> status escrito
      settings: state.settings && typeof state.settings === 'object' ? state.settings : {}, // escolhas do /openspeak
    };
  } catch {
    return { messages: {}, voice: {}, settings: {} };
  }
}

function saveState(state) {
  try {
    fs.writeFileSync(STATE_FILE, JSON.stringify(state, null, 2));
  } catch (err) {
    log('aviso: não foi possível salvar o estado:', err.message);
  }
}

function explain(err) {
  // 50013 = Missing Permissions, 50001 = Missing Access, 10003 = Unknown Channel
  if (err?.code === 50013) return 'sem permissão (veja as permissões no README)';
  if (err?.code === 50001) return 'o bot não tem acesso ao canal';
  if (err?.code === 10003) return 'canal não encontrado (confira o ID)';
  return err?.message ?? String(err);
}

/** /openspeak: escolhe o canal do chat e liga/desliga os avisos pelo Discord. */
const ADMIN_COMMAND = new SlashCommandBuilder()
  .setName('openspeak')
  .setDescription('Avisos de transmissões do OpenSpeak')
  .setDefaultMemberPermissions(PermissionFlagsBits.ManageGuild)
  .setContexts(InteractionContextType.Guild)
  .addSubcommand(sub => sub
    .setName('canal')
    .setDescription('Canal de texto onde o bot avisa quem está ao vivo')
    .addChannelOption(opt => opt
      .setName('canal').setDescription('Canal de texto').setRequired(true)
      .addChannelTypes(ChannelType.GuildText, ChannelType.GuildAnnouncement)))
  .addSubcommand(sub => sub
    .setName('avisos')
    .setDescription('Liga ou desliga um dos avisos')
    .addStringOption(opt => opt
      .setName('aviso').setDescription('Qual aviso').setRequired(true)
      .addChoices(
        { name: 'Chat (mensagem no canal de texto)', value: 'chat' },
        { name: 'Status do canal de voz', value: 'voz' },
        { name: 'Status do bot', value: 'status' },
      ))
    .addBooleanOption(opt => opt.setName('ligado').setDescription('Ligar ou desligar').setRequired(true)))
  .addSubcommand(sub => sub.setName('ver').setDescription('Mostra a configuração atual dos avisos'));

/** Comandos para qualquer pessoa (também na DM com o bot). */
const PUBLIC_CONTEXTS = [InteractionContextType.Guild, InteractionContextType.BotDM];
const COMMANDS = [
  ADMIN_COMMAND,
  new SlashCommandBuilder().setName('aovivo').setDescription('Quem está transmitindo no OpenSpeak agora')
    .setContexts(...PUBLIC_CONTEXTS),
  new SlashCommandBuilder().setName('servidor').setDescription('Usuários online, transmissões e tráfego do servidor OpenSpeak')
    .setContexts(...PUBLIC_CONTEXTS),
];
const COMMAND_NAMES = new Set(COMMANDS.map(c => c.name));

/** Tipo da atividade no status do bot (config presence.type). */
const ACTIVITY_TYPE = {
  playing: ActivityType.Playing, watching: ActivityType.Watching,
  listening: ActivityType.Listening, competing: ActivityType.Competing,
};

const NOTICE_NAME = { chat: 'Chat', voiceStatus: 'Status do canal de voz', presence: 'Status do bot' };

class LiveBot {
  constructor(baseConfig, client) {
    this.baseConfig = baseConfig;
    this.client = client;
    this.state = loadState();
    const config = (this.config = applySettings(baseConfig, this.state.settings));
    this.known = new Map(); // chave -> stream (último visto)
    this.watched = new Map(); // chave -> apelidos de quem assistiu em algum momento
    this.failures = 0;
    this.voiceText = '';
    // Status já escrito por canal de voz. Com canal fixo, a primeira escrita é
    // forçada (limpa um status antigo); no automático só se limpa o que o bot escreveu.
    this.voiceCurrent = new Map(config.voiceStatus.channelId ? [[config.voiceStatus.channelId, '\0']]
      : Object.entries(this.state.voice)); // limpa o que ficou de antes de um reinício
    this.voiceQueue = Promise.resolve();
    this.lastPresence = null;
    // Status do bot: alterna entre quem está ao vivo e a carga do servidor.
    this.presenceTexts = [];
    this.presenceIndex = 0;
    this.presenceMain = null;
    this.presenceLive = false;
    this.lastPresenceLive = null;
    this.pendingPresence = null;
    this.presenceAt = 0;
    this.presenceTimer = null;
    this.streams = [];
    this.stats = parseStats(null); // carga do servidor (desconhecida até a primeira resposta)
    this.queue = Promise.resolve(); // consultas e webhooks, um de cada vez
    this.liveUrl = new URL('/api/live', config.openspeakUrl).toString();
  }

  async fetchStreams() {
    const res = await fetch(this.liveUrl, { signal: AbortSignal.timeout(8000), headers: { accept: 'application/json' } });
    if (!res.ok) throw new Error(`HTTP ${res.status} em ${this.liveUrl}`);
    const body = await res.json();
    return { streams: parseLive(body), stats: parseStats(body) };
  }

  enqueue(job) {
    this.queue = this.queue.then(job).catch(err => log('erro:', err));
    return this.queue;
  }

  /** Consulta periódica ao /api/live (com o webhook, só uma rede de segurança). */
  tick() {
    return this.enqueue(async () => {
      let streams, stats;
      try {
        ({ streams, stats } = await this.fetchStreams());
        this.online();
      } catch (err) {
        this.failures++;
        if (this.failures === 1) log('falha ao consultar o OpenSpeak:', err.message);
        if (this.failures < OFFLINE_AFTER_FAILURES) return; // mantém o último estado
        if (this.failures === OFFLINE_AFTER_FAILURES) log('servidor fora do ar: considerando que ninguém está ao vivo');
        streams = [];
        stats = parseStats(null);
      }
      await this.apply(streams, stats);
    });
  }

  /** O servidor avisou pelo webhook: atualiza na hora. */
  push(streams, stats) {
    return this.enqueue(async () => {
      this.online();
      await this.apply(streams, stats);
    });
  }

  online() {
    if (this.failures >= OFFLINE_AFTER_FAILURES) log('servidor OpenSpeak respondeu de novo');
    this.failures = 0;
  }

  async apply(streams, stats = this.stats) {
    // Mensagens de streams que já existiam antes de um reinício do bot também contam.
    const previous = new Set([...this.known.keys(), ...Object.keys(this.state.messages)]);
    const { started, stopped } = diff(previous, streams);
    const current = new Map(streams.map(s => [streamKey(s), s]));

    for (const s of started) log(`ao vivo: ${s.owner} — ${appLabel(s)} (${s.name})`);
    this.trackWatchers(current);
    // Avisos já publicados terminam mesmo com o chat desligado ou em outro canal.
    for (const key of stopped) {
      await this.finish(key, this.known.get(key));
      this.watched.delete(key);
    }
    if (this.config.chat.enabled && this.config.chat.channelId) {
      for (const s of started) await this.announce(s);
      for (const [key, s] of current) {
        const before = this.known.get(key);
        if (before && (before.app !== s.app || before.name !== s.name || before.paused !== s.paused)) await this.refresh(key, s);
      }
    }
    this.known = current;
    this.streams = streams;
    this.stats = stats;

    if (this.config.voiceStatus.enabled) {
      this.voiceText = voiceStatusText(this.config.voiceStatus, streams, stats);
      await this.syncVoice();
    }
    if (this.config.presence.enabled) this.updatePresence(streams, stats);
  }

  /**
   * Acumula quem assistiu: quem saiu antes do fim também entra na lista. Guardado
   * junto do aviso publicado para sobreviver a um reinício do bot.
   */
  trackWatchers(current) {
    let changed = false;
    for (const [key, s] of current) {
      const saved = this.state.messages[key];
      const names = this.watched.get(key) ?? new Set(saved?.watchers);
      for (const name of s.viewerNames) names.add(name);
      this.watched.set(key, names);
      if (saved && names.size !== (saved.watchers?.length ?? 0)) {
        saved.watchers = [...names];
        changed = true;
      }
    }
    if (changed) saveState(this.state);
  }

  // ----------------------------------------------------------------- chat

  embedFor(s, ended = false, watchers = []) {
    const embed = new EmbedBuilder()
      .setColor(ended ? 0x5c6370 : s.paused ? 0xe5a50a : 0xe5484d)
      .setTitle(ended ? `⚫ ${s.owner || 'Alguém'} encerrou a transmissão` : `🔴 ${s.owner || 'Alguém'} está ao vivo no OpenSpeak`)
      .addFields({ name: 'Transmitindo', value: appLabel(s), inline: true });
    if (s.name) embed.setDescription(s.name);
    if (ended) {
      embed.addFields({ name: 'Duração', value: formatDuration(Date.now() - s.started), inline: true });
      // Só no SFU: no P2P a mídia não passa pelo servidor, que não sabe quanto foi.
      const media = mediaText(s.media);
      if (media) embed.addFields({ name: 'Dados', value: media, inline: true });
      embed.addFields({ name: 'Assistiram', value: watchersText(watchers) });
    }
    else if (s.paused) embed.addFields({ name: 'Situação', value: 'Pausada', inline: true });
    if (!ended) embed.setURL(this.config.openspeakUrl);
    if (s.started) embed.setTimestamp(new Date(s.started));
    return embed;
  }

  async textChannel() {
    const channel = await this.client.channels.fetch(this.config.chat.channelId);
    if (!channel?.isTextBased?.()) throw new Error('chat.channelId não é um canal de texto');
    return channel;
  }

  async announce(s) {
    try {
      const channel = await this.textChannel();
      const mention = this.config.chat.mention?.trim();
      const message = await channel.send({
        content: mention || undefined,
        embeds: [this.embedFor(s)],
        allowedMentions: mention ? { parse: ['everyone', 'roles', 'users'] } : { parse: [] },
      });
      this.state.messages[streamKey(s)] = { channelId: channel.id, messageId: message.id, stream: s };
      saveState(this.state);
    } catch (err) {
      log('chat: falha ao avisar:', explain(err));
    }
  }

  async message(key) {
    const saved = this.state.messages[key];
    if (!saved) return null;
    const channel = await this.client.channels.fetch(saved.channelId);
    return channel.messages.fetch(saved.messageId);
  }

  async refresh(key, s) {
    try {
      const msg = await this.message(key);
      if (msg) await msg.edit({ embeds: [this.embedFor(s)] });
      if (this.state.messages[key]) this.state.messages[key].stream = s;
      saveState(this.state);
    } catch (err) {
      log('chat: falha ao atualizar aviso:', explain(err));
    }
  }

  async finish(key, last) {
    const saved = this.state.messages[key];
    if (!saved) return;
    const s = last ?? saved?.stream;
    if (s) log(`encerrou: ${s.owner} — ${appLabel(s)}`);
    try {
      const msg = await this.message(key);
      if (msg && this.config.chat.onEnd === 'delete') await msg.delete();
      else if (msg && this.config.chat.onEnd === 'edit' && s) {
        const watchers = [...(saved.watchers ?? []), ...(this.watched.get(key) ?? [])];
        await msg.edit({ content: null, embeds: [this.embedFor(s, true, watchers)] });
      }
    } catch (err) {
      if (err?.code !== 10008) log('chat: falha ao encerrar aviso:', explain(err)); // 10008 = mensagem já apagada
    }
    delete this.state.messages[key];
    saveState(this.state);
  }

  // --------------------------------------------- status do canal de voz

  /** Canal fixo da config, ou os canais de voz com alguém (que não seja bot) conectado. */
  voiceTargets() {
    if (this.config.voiceStatus.channelId) return [this.config.voiceStatus.channelId];
    const targets = [];
    for (const guild of this.client.guilds.cache.values()) {
      if (this.config.guildId && guild.id !== this.config.guildId) continue;
      for (const channel of guild.channels.cache.values()) {
        if (channel.type === ChannelType.GuildVoice && channel.members.some(m => !m.user.bot)) targets.push(channel.id);
      }
    }
    return targets;
  }

  /** Enfileirado: roda a cada consulta e quando alguém entra/sai de uma call. */
  syncVoice() {
    this.voiceQueue = this.voiceQueue.then(async () => {
      for (const { channelId, status } of planVoiceUpdates(this.voiceCurrent, this.voiceTargets(), this.voiceText)) {
        try {
          // Não há método no discord.js: PUT /channels/{id}/voice-status { status }.
          await this.client.rest.put(`${Routes.channel(channelId)}/voice-status`, { body: { status } });
        } catch (err) {
          const name = this.client.channels.cache.get(channelId)?.name ?? channelId;
          log(`status do canal de voz "${name}":`, explain(err));
        }
        // Mesmo com erro: não insiste a cada consulta, tenta de novo na próxima mudança.
        if (status) this.voiceCurrent.set(channelId, status);
        else this.voiceCurrent.delete(channelId);
        if (!this.config.voiceStatus.channelId) {
          this.state.voice = Object.fromEntries(this.voiceCurrent);
          saveState(this.state);
        }
      }
    }).catch(err => log('status do canal de voz:', explain(err)));
    return this.voiceQueue;
  }

  // ------------------------------------------------------ status do bot

  updatePresence(streams, stats) {
    const main = presenceText(this.config.presence, streams, stats);
    if (main !== this.presenceMain) this.presenceIndex = 0; // mudou quem está ao vivo: mostra na hora
    this.presenceMain = main;
    const server = serverText(this.config.presence, stats);
    // Só alterna com alguém ao vivo. Sem transmissão, um texto fixo: a carga do
    // servidor se tem gente conectada, senão o "idle".
    this.presenceTexts = (streams.length ? [main, server] : [stats?.clients > 0 && server ? server : main]).filter(Boolean);
    this.presenceLive = streams.length > 0;
    this.showPresence();
  }

  /** Chamado a cada presence.rotateSeconds. */
  rotatePresence() {
    if (!this.config.presence.enabled || this.presenceTexts.length < 2) return;
    this.presenceIndex = (this.presenceIndex + 1) % this.presenceTexts.length;
    this.showPresence();
  }

  showPresence() {
    const texts = this.presenceTexts;
    this.setPresence(texts.length ? texts[this.presenceIndex % texts.length] : '', this.presenceLive);
  }

  /**
   * Com o webhook, entradas e saídas chegam em rajadas: o Discord limita trocas de
   * status, então aplica no máximo uma a cada PRESENCE_MIN_MS (sempre a mais recente).
   */
  setPresence(text, live) {
    this.pendingPresence = { text, live };
    const wait = this.presenceAt + PRESENCE_MIN_MS - Date.now();
    if (wait <= 0) return this.flushPresence();
    this.presenceTimer ??= setTimeout(() => {
      this.presenceTimer = null;
      this.flushPresence();
    }, wait);
  }

  flushPresence() {
    const { text, live } = this.pendingPresence;
    if (!this.config.presence.enabled || (text === this.lastPresence && live === this.lastPresenceLive)) return;
    this.lastPresence = text;
    this.lastPresenceLive = live;
    this.presenceAt = Date.now();
    const type = ACTIVITY_TYPE[this.config.presence.type];
    // "custom" mostra só o texto; os outros tipos aparecem como "Jogando <texto>" etc.
    const activity = type === undefined ? { type: ActivityType.Custom, name: 'OpenSpeak', state: text } : { type, name: text };
    this.client.user.setPresence({ status: live ? 'online' : 'idle', activities: text ? [activity] : [] });
  }

  // ------------------------------------------------------- /openspeak

  /**
   * Comandos globais: é o que faz o Discord mostrar "Supports Slash Commands" no
   * perfil do bot. Um create por comando (sobrescreve pelo nome) em vez de set(),
   * para não apagar os outros comandos da aplicação, como o de abrir a atividade.
   */
  async registerCommands() {
    const existing = await this.client.application.commands.fetch();
    for (const command of COMMANDS) {
      const json = command.toJSON();
      const current = existing.find(c => c.name === json.name);
      if (current?.equals(json)) continue;
      await this.client.application.commands.create(json);
      log(`comando /${json.name} registrado`);
    }
  }

  /** Versões antigas registravam o /openspeak por servidor: com o global, ficaria duplicado. */
  async removeGuildCommands(guild) {
    try {
      for (const command of (await guild.commands.fetch()).values())
        if (COMMAND_NAMES.has(command.name)) await command.delete();
    } catch (err) {
      if (err?.code !== 50001) log(`comandos antigos em "${guild.name}":`, explain(err));
    }
  }

  saveSettings(change) {
    const settings = this.state.settings;
    for (const [section, values] of Object.entries(change)) settings[section] = { ...settings[section], ...values };
    this.state.settings = settings;
    saveState(this.state);
    this.config = applySettings(this.baseConfig, settings);
  }

  describe() {
    const c = this.config;
    const line = section => `${c[section].enabled ? '🟢' : '⚪'} **${NOTICE_NAME[section]}**`;
    const voice = c.voiceStatus.channelId ? `<#${c.voiceStatus.channelId}>` : 'calls com alguém conectado';
    const chat = c.chat.channelId ? `<#${c.chat.channelId}>` : 'nenhum canal escolhido — use `/openspeak canal`';
    const v = statsVars(this.stats);
    return [`${line('chat')}: ${chat}`, `${line('voiceStatus')}: ${voice}`, `${line('presence')}`,
      `Transmissões ao vivo agora: **${this.streams.length}**`,
      `Servidor: **${v.usuarios}** online, **${v.transmissoes}** transmissões, **${v.assistindo}** assistindo, **${v.mbps}** Mbps`,
    ].join('\n');
  }

  async onCommand(interaction) {
    if (interaction.commandName === 'aovivo') return this.onLiveCommand(interaction);
    if (interaction.commandName === 'servidor') return this.onServerCommand(interaction);
    const reply = content => interaction.reply({ content, flags: MessageFlags.Ephemeral, allowedMentions: { parse: [] } });
    if (this.config.guildId && interaction.guildId !== this.config.guildId)
      return reply('Os avisos do OpenSpeak estão configurados para outro servidor.');
    if (!interaction.memberPermissions?.has(PermissionFlagsBits.ManageGuild))
      return reply('Só quem pode **Gerenciar servidor** configura os avisos.');
    const sub = interaction.options.getSubcommand();

    if (sub === 'canal') {
      const channel = interaction.options.getChannel('canal', true);
      const perms = channel.permissionsFor?.(this.client.user);
      const needed = [PermissionFlagsBits.ViewChannel, PermissionFlagsBits.SendMessages, PermissionFlagsBits.EmbedLinks];
      if (perms && !perms.has(needed))
        return reply(`Não consigo escrever em <#${channel.id}>: preciso de **Ver canal**, **Enviar mensagens** e **Inserir links** lá.`);
      this.saveSettings({ chat: { channelId: channel.id, enabled: true } });
      log(`/openspeak: chat em #${channel.name} (por ${interaction.user.tag})`);
      await reply(`Pronto: vou avisar em <#${channel.id}> quando alguém começar a transmitir.`);
      return;
    }

    if (sub === 'avisos') {
      const section = NOTICES[interaction.options.getString('aviso', true)];
      const enabled = interaction.options.getBoolean('ligado', true);
      this.saveSettings({ [section]: { enabled } });
      log(`/openspeak: ${NOTICE_NAME[section]} ${enabled ? 'ligado' : 'desligado'} (por ${interaction.user.tag})`);
      if (section === 'voiceStatus' && !enabled) {
        this.voiceText = '';
        this.syncVoice();
      }
      if (section === 'presence' && !enabled) {
        this.lastPresence = this.presenceMain = null;
        this.presenceTexts = [];
        this.client.user.setPresence({ status: 'online', activities: [] });
      }
      if (enabled) this.enqueue(() => this.apply(this.streams));
      const hint = section === 'chat' && enabled && !this.config.chat.channelId ? '\nFalta escolher o canal: `/openspeak canal`.' : '';
      await reply(`${NOTICE_NAME[section]} ${enabled ? 'ligado' : 'desligado'}.${hint}`);
      return;
    }

    await reply(this.describe());
  }

  /** /aovivo: quem está transmitindo (só streams com "Mostrar ao vivo no bot do Discord"). */
  async onLiveCommand(interaction) {
    const streams = this.streams;
    const embed = new EmbedBuilder().setColor(streams.length ? 0xe5484d : 0x5c6370).setURL(this.config.openspeakUrl)
      .setTitle(streams.length ? `🔴 ${streams.length} ao vivo no OpenSpeak` : 'Ninguém ao vivo no OpenSpeak agora');
    if (streams.length) {
      const now = Date.now();
      const lines = streams.slice(0, 15).map(s => {
        const parts = [`**${s.owner || 'Alguém'}** — ${appLabel(s)}`];
        if (s.channel) parts.push(s.channel);
        parts.push(`👀 ${s.viewers}`);
        if (s.started) parts.push(formatDuration(now - s.started));
        return `${s.paused ? '⏸️' : '🔴'} ${parts.join(' · ')}`;
      });
      if (streams.length > 15) lines.push(`… e mais ${streams.length - 15}`);
      embed.setDescription(lines.join('\n'));
    }
    await interaction.reply({ embeds: [embed], allowedMentions: { parse: [] } });
  }

  /** /servidor: carga do servidor (contagens agregadas do /api/live). */
  async onServerCommand(interaction) {
    const v = statsVars(this.stats);
    const offline = this.failures >= OFFLINE_AFTER_FAILURES;
    const embed = new EmbedBuilder().setColor(offline ? 0x5c6370 : 0x3ba55d).setURL(this.config.openspeakUrl)
      .setTitle(offline ? '⚫ Servidor OpenSpeak fora do ar' : '🟢 Servidor OpenSpeak')
      .addFields(
        { name: 'Online', value: v.usuarios, inline: true },
        { name: 'Transmissões', value: v.transmissoes, inline: true },
        { name: 'Assistindo', value: v.assistindo, inline: true },
        { name: 'Tráfego', value: this.stats.mbps == null ? '— (P2P: a mídia não passa pelo servidor)' : `${v.mbps} Mbps`, inline: true },
      );
    await interaction.reply({ embeds: [embed], allowedMentions: { parse: [] } });
  }

  async shutdown() {
    if (!this.config.voiceStatus.enabled) return;
    this.voiceText = '';
    await this.syncVoice().catch(() => {});
  }
}

const { config, token } = loadConfig();
// GuildVoiceStates (não é privilegiada): saber quais calls têm gente.
const client = new Client({ intents: [GatewayIntentBits.Guilds, GatewayIntentBits.GuildVoiceStates] });
let bot;

client.once('clientReady', async () => {
  bot = new LiveBot(config, client);
  const c = bot.config;
  log(`conectado como ${client.user.tag}; consultando ${new URL('/api/live', c.openspeakUrl)} a cada ${c.pollSeconds} s`);
  const voice = !c.voiceStatus.enabled ? 'desligado' : c.voiceStatus.channelId ? 'ligado (canal fixo)' : 'ligado (calls com gente)';
  const chat = !c.chat.enabled ? 'desligado' : c.chat.channelId ? 'ligado' : 'ligado, sem canal (use /openspeak canal)';
  log(`avisos: status do canal de voz ${voice}, chat ${chat}, status do bot ${c.presence.enabled ? 'ligado' : 'desligado'}`);
  if (!c.chat.enabled && !c.voiceStatus.enabled && !c.presence.enabled)
    log('todos os avisos estão desligados: ligue com /openspeak avisos no Discord');
  try {
    await bot.registerCommands();
  } catch (err) {
    log('comandos:', explain(err), '(convide o bot com o escopo applications.commands)');
  }
  for (const guild of client.guilds.cache.values()) await bot.removeGuildCommands(guild);
  if (c.webhook.port) {
    try {
      await startWebhook({ ...c.webhook, onLive: (streams, stats) => bot.push(streams, stats) });
      log(`webhook: escutando em http://${c.webhook.host}:${c.webhook.port}/live` +
        (c.webhook.secret ? ' (com segredo)' : ' (sem segredo)'));
    } catch (err) {
      log(`webhook: não foi possível escutar na porta ${c.webhook.port}: ${err.message} (seguindo só com a consulta)`);
    }
  }
  await bot.tick();
  setInterval(() => bot.tick(), c.pollSeconds * 1000);
  setInterval(() => bot.rotatePresence(), c.presence.rotateSeconds * 1000);
});

client.on('interactionCreate', interaction => {
  if (!bot || !interaction.isChatInputCommand() || !COMMAND_NAMES.has(interaction.commandName)) return;
  bot.onCommand(interaction).catch(err => log(`/${interaction.commandName}:`, explain(err)));
});

client.on('voiceStateUpdate', (before, after) => {
  if (bot?.config.voiceStatus.enabled && !bot.config.voiceStatus.channelId && before.channelId !== after.channelId)
    bot.syncVoice().catch(err => log('erro:', err));
});

for (const signal of ['SIGINT', 'SIGTERM']) {
  process.on(signal, async () => {
    log('encerrando');
    await bot?.shutdown();
    await client.destroy();
    process.exit(0);
  });
}

client.login(token).catch(async err => {
  console.error('Login no Discord falhou:', err.message);
  await client.destroy();
  process.exitCode = 1;
});
