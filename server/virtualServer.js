// Servidor virtual: canais, clientes e o registro de streams.
//
// Reproduz a parte do servidor TeamSpeak que media a transmissão P2P: ele nunca
// toca em mídia, apenas guarda o estado dos streams, aplica privacidade/limites
// e encaminha a sinalização WebRTC (`streamsignaling` -> `notifystreamsignaling`)
// entre o transmissor e cada espectador.

import {
  StreamAccess, StreamMode, StreamType, LeaveReason, ReturnCode, CAPABILITIES,
  MAX_STREAMS_PER_CLIENT, MAX_THUMBNAIL_BYTES, MAX_SIGNALING_BYTES,
} from './protocol.js';

class CommandError extends Error {
  constructor(code, message = code) {
    super(message);
    this.code = code;
  }
}

const fail = (code, message) => { throw new CommandError(code, message); };

const DEFAULT_CHANNELS = [
  { name: 'Lobby', description: 'Canal padrão' },
  { name: 'Jogos', description: 'Partidas e transmissões de gameplay' },
  { name: 'Trabalho', description: 'Reuniões e compartilhamento de tela' },
  { name: 'AFK', description: 'Ausente' },
];

function clampInt(value, min, max, fallback) {
  const n = Number(value);
  if (!Number.isFinite(n)) return fallback;
  return Math.min(max, Math.max(min, Math.round(n)));
}

function cleanString(value, maxLength, fallback = '') {
  if (typeof value !== 'string') return fallback;
  return value.replace(/[\u0000-\u001f]/g, '').trim().slice(0, maxLength);
}

export class VirtualServer {
  /**
   * @param {object} options
   * @param {(clid:number, message:object) => void} options.send
   * @param {string} [options.name]
   * @param {object[]} [options.iceServers]
   */
  constructor({ send, name = 'OpenSpeak', iceServers = [], idPrefix = '' }) {
    this.send = send;
    this.name = name;
    this.iceServers = iceServers;
    this.clients = new Map(); // clid -> client
    this.channels = new Map(); // cid -> channel
    this.streams = new Map(); // streamIdentifier -> stream
    this.nextClid = 1;
    this.nextStreamId = 1;
    this.idPrefix = idPrefix;
    let cid = 1;
    for (const ch of DEFAULT_CHANNELS) {
      this.channels.set(cid, { cid, order: cid, ...ch });
      cid++;
    }
    this.defaultChannel = 1;
  }

  // ---------------------------------------------------------------- conexão

  connect() {
    const clid = this.nextClid++;
    this.clients.set(clid, { clid, loggedIn: false, nickname: '', uid: '', cid: 0, inputMuted: false, outputMuted: false });
    return clid;
  }

  snapshot() {
    return {
      nextClid: this.nextClid, nextStreamId: this.nextStreamId, idPrefix: this.idPrefix,
      clients: [...this.clients],
      streams: [...this.streams].map(([id, s]) => [id, { ...s,
        viewers: [...s.viewers], pending: [...s.pending], banned: [...s.banned] }]),
    };
  }

  restore(state) {
    if (!state) return;
    this.nextClid = state.nextClid;
    this.nextStreamId = state.nextStreamId;
    this.idPrefix = state.idPrefix || '';
    this.clients = new Map(state.clients);
    this.streams = new Map(state.streams.map(([id, s]) => [id, { ...s,
      viewers: new Set(s.viewers), pending: new Map(s.pending), banned: new Set(s.banned) }]));
  }

  view(clid) {
    if (!this.clients.get(clid)?.loggedIn) fail(ReturnCode.NOT_LOGGED_IN);
    return { clid, server: { name: this.name, capabilityExtensions: [...CAPABILITIES] },
      iceServers: this.iceServers, channels: [...this.channels.values()],
      clients: [...this.clients.values()].filter(c => c.loggedIn).map(c => this.#publicClient(c)),
      streams: [...this.streams.values()].map(s => this.#publicStream(s)) };
  }

  disconnect(clid) {
    const client = this.clients.get(clid);
    if (!client) return;
    if (client.loggedIn) {
      this.#detachFromStreams(client, LeaveReason.LEFT);
      this.clients.delete(clid);
      this.#broadcast({ cmd: 'notifyclientleftview', args: { clid } });
    } else {
      this.clients.delete(clid);
    }
  }

  /** Executa um comando e devolve `data` da resposta, ou lança CommandError. */
  handle(clid, cmd, args = {}) {
    const client = this.clients.get(clid);
    if (!client) fail(ReturnCode.CLIENT_NOT_FOUND);
    if (cmd !== 'login' && !client.loggedIn) fail(ReturnCode.NOT_LOGGED_IN);
    const handler = this.#commands[cmd];
    if (!handler) fail(ReturnCode.UNKNOWN_COMMAND, `comando desconhecido: ${cmd}`);
    return handler.call(this, client, args ?? {});
  }

  // --------------------------------------------------------------- comandos

  #commands = {
    login(client, args) {
      if (client.loggedIn) fail(ReturnCode.INVALID_ARGS, 'já autenticado');
      const nickname = cleanString(args.nickname, 32);
      const uid = cleanString(args.uid, 64);
      if (!nickname || !uid) fail(ReturnCode.INVALID_ARGS, 'nickname e uid são obrigatórios');
      client.nickname = nickname;
      client.uid = uid;
      client.cid = this.defaultChannel;
      client.loggedIn = true;
      this.#broadcast({ cmd: 'notifycliententerview', args: { client: this.#publicClient(client) } }, client.clid);
      return {
        clid: client.clid,
        server: { name: this.name, capabilityExtensions: [...CAPABILITIES] },
        iceServers: this.iceServers,
        channels: [...this.channels.values()],
        clients: [...this.clients.values()].filter(c => c.loggedIn).map(c => this.#publicClient(c)),
        streams: [...this.streams.values()].map(s => this.#publicStream(s)),
      };
    },

    clientmove(client, args) {
      const cid = Number(args.cid);
      if (!this.channels.has(cid)) fail(ReturnCode.CHANNEL_NOT_FOUND);
      if (client.cid === cid) return {};
      // Streams pertencem ao canal: sair do canal encerra os próprios streams
      // e a participação nos streams do canal anterior.
      this.#detachFromStreams(client, LeaveReason.LEFT);
      client.cid = cid;
      this.#broadcast({ cmd: 'notifyclientmoved', args: { clid: client.clid, cid } });
      return {};
    },

    clientupdate(client, args) {
      if ('nickname' in args) {
        const nickname = cleanString(args.nickname, 32);
        if (!nickname) fail(ReturnCode.INVALID_ARGS);
        client.nickname = nickname;
      }
      if ('inputMuted' in args) client.inputMuted = !!args.inputMuted;
      if ('outputMuted' in args) client.outputMuted = !!args.outputMuted;
      this.#broadcast({ cmd: 'notifyclientupdated', args: { client: this.#publicClient(client) } });
      return {};
    },

    setupstream(client, args) {
      const mode = Number(args.mode ?? StreamMode.P2P);
      if (mode !== StreamMode.P2P) fail(ReturnCode.MODE_NOT_SUPPORTED, 'o servidor só suporta STREAM_P2P');
      const owned = [...this.streams.values()].filter(s => s.ownerClid === client.clid);
      if (owned.length >= MAX_STREAMS_PER_CLIENT) fail(ReturnCode.STREAM_LIMIT);
      const stream = {
        streamIdentifier: `${this.idPrefix}s${this.nextStreamId++}`,
        ownerClid: client.clid,
        cid: client.cid,
        mode,
        created: Date.now(),
        paused: false,
        thumbnail: null,
        viewers: new Set(),
        pending: new Map(), // clid -> { codecs, since }
        banned: new Set(), // uids
        ...this.#sanitizeStreamConfig(args, {
          name: `Stream de ${client.nickname}`, type: StreamType.SCREEN, access: StreamAccess.PUBLIC,
          bitrate: 3_000_000, viewerLimit: 0, audio: false, quality: { width: 1280, height: 720, fps: 30 },
        }),
      };
      this.streams.set(stream.streamIdentifier, stream);
      this.#broadcast({ cmd: 'notifystreamstarted', args: { stream: this.#publicStream(stream) } });
      return { streamIdentifier: stream.streamIdentifier };
    },

    updatestream(client, args) {
      const stream = this.#ownStream(client, args.streamIdentifier);
      const accessBefore = stream.access;
      Object.assign(stream, this.#sanitizeStreamConfig(args, stream));
      if ('paused' in args) stream.paused = !!args.paused;
      if (stream.access !== StreamAccess.PUBLIC) stream.thumbnail = null;
      this.#broadcast({ cmd: 'notifystreamupdated', args: { stream: this.#publicStream(stream), accessChanged: accessBefore !== stream.access } });
      return {};
    },

    stopstream(client, args) {
      const stream = this.#ownStream(client, args.streamIdentifier);
      this.#stopStream(stream, LeaveReason.STREAM_STOPPED);
      return {};
    },

    streamthumbnail(client, args) {
      const stream = this.#ownStream(client, args.streamIdentifier);
      const thumbnail = typeof args.thumbnail === 'string' ? args.thumbnail : null;
      if (!thumbnail || !thumbnail.startsWith('data:image/')) fail(ReturnCode.INVALID_ARGS);
      if (thumbnail.length > MAX_THUMBNAIL_BYTES) fail(ReturnCode.PAYLOAD_TOO_LARGE);
      // Miniaturas só são publicadas para streams públicos (igual ao TS).
      if (stream.access !== StreamAccess.PUBLIC) return { published: false };
      stream.thumbnail = thumbnail;
      this.#broadcastToChannel(stream.cid, {
        cmd: 'notifystreamthumbnail', args: { streamIdentifier: stream.streamIdentifier, thumbnail },
      }, client.clid);
      return { published: true };
    },

    requeststreaminfo(client, args) {
      const stream = this.#stream(args.streamIdentifier);
      const info = this.#publicStream(stream);
      if (stream.ownerClid === client.clid) {
        info.viewers = [...stream.viewers];
        info.pending = [...stream.pending.keys()];
        info.banned = [...stream.banned];
      }
      this.send(client.clid, { type: 'notify', cmd: 'notifystreaminfo', args: { stream: info } });
      return { stream: info };
    },

    joinstreamrequest(client, args) {
      const stream = this.#stream(args.streamIdentifier);
      if (stream.ownerClid === client.clid) fail(ReturnCode.INVALID_ARGS, 'não é possível assistir ao próprio stream');
      if (stream.cid !== client.cid) fail(ReturnCode.NOT_IN_CHANNEL);
      if (stream.banned.has(client.uid)) fail(ReturnCode.BANNED);
      if (stream.viewers.has(client.clid)) fail(ReturnCode.ALREADY_JOINED);
      if (stream.viewerLimit > 0 && stream.viewers.size >= stream.viewerLimit) fail(ReturnCode.VIEWER_LIMIT);
      const codecs = Array.isArray(args.codecs) ? args.codecs.filter(c => typeof c === 'string').slice(0, 16) : [];
      stream.pending.set(client.clid, { codecs, since: Date.now() });
      this.send(stream.ownerClid, {
        type: 'notify', cmd: 'notifyjoinstreamrequest',
        args: {
          streamIdentifier: stream.streamIdentifier,
          clid: client.clid, nickname: client.nickname, uid: client.uid, codecs,
        },
      });
      return { pending: true };
    },

    respondjoinstreamrequest(client, args) {
      const stream = this.#ownStream(client, args.streamIdentifier);
      const clid = Number(args.clid);
      const request = stream.pending.get(clid);
      if (!request) fail(ReturnCode.NO_PENDING_REQUEST);
      const viewer = this.clients.get(clid);
      if (!viewer || viewer.cid !== stream.cid) {
        stream.pending.delete(clid);
        fail(ReturnCode.CLIENT_NOT_FOUND);
      }
      if (args.accept) {
        if (stream.viewerLimit > 0 && stream.viewers.size >= stream.viewerLimit) fail(ReturnCode.VIEWER_LIMIT);
        stream.pending.delete(clid);
        stream.viewers.add(clid);
        this.send(clid, {
          type: 'notify', cmd: 'notifyrespondjoinstreamrequest',
          // peerClid: com quem o espectador negocia (aqui sempre o dono; o SFU só existe no servidor nativo).
          args: { streamIdentifier: stream.streamIdentifier, accepted: true, ownerClid: stream.ownerClid, peerClid: stream.ownerClid },
        });
        this.send(stream.ownerClid, {
          type: 'notify', cmd: 'notifystreamclientjoined',
          args: { streamIdentifier: stream.streamIdentifier, clid, codecs: request.codecs },
        });
        this.#notifyAttendees(stream);
      } else {
        stream.pending.delete(clid);
        const ban = !!args.ban;
        if (ban) this.#ban(stream, viewer.uid);
        this.send(clid, {
          type: 'notify', cmd: 'notifyrespondjoinstreamrequest',
          args: { streamIdentifier: stream.streamIdentifier, accepted: false, reason: ban ? LeaveReason.BANNED : LeaveReason.DENIED },
        });
      }
      return {};
    },

    removeclientfromstream(client, args) {
      const stream = this.#stream(args.streamIdentifier);
      const target = Number(args.clid ?? client.clid);
      const isOwner = stream.ownerClid === client.clid;
      if (!isOwner && target !== client.clid) fail(ReturnCode.PERMISSION_DENIED);
      let reason = isOwner && target !== client.clid ? Number(args.reason ?? LeaveReason.KICKED) : LeaveReason.LEFT;
      if (![LeaveReason.LEFT, LeaveReason.KICKED, LeaveReason.BANNED, LeaveReason.FAILED].includes(reason)) reason = LeaveReason.KICKED;
      if (reason === LeaveReason.BANNED) {
        const uid = this.clients.get(target)?.uid;
        if (uid) this.#ban(stream, uid);
      }
      this.#removeViewer(stream, target, reason);
      return {};
    },

    streamunban(client, args) {
      const stream = this.#ownStream(client, args.streamIdentifier);
      const uid = cleanString(args.uid, 64);
      if (stream.banned.delete(uid)) {
        this.send(client.clid, { type: 'notify', cmd: 'notifystreambanremoved', args: { streamIdentifier: stream.streamIdentifier, uid } });
      }
      return {};
    },

    streamsignaling(client, args) {
      const stream = this.#stream(args.streamIdentifier);
      const target = Number(args.clid);
      const payload = args.payload;
      if (!payload || typeof payload !== 'object' || typeof payload.type !== 'string') fail(ReturnCode.INVALID_ARGS);
      if (JSON.stringify(payload).length > MAX_SIGNALING_BYTES) fail(ReturnCode.PAYLOAD_TOO_LARGE);
      // Só pares legítimos conversam: dono <-> espectador aceito.
      const ownerToViewer = stream.ownerClid === client.clid && stream.viewers.has(target);
      const viewerToOwner = stream.ownerClid === target && stream.viewers.has(client.clid);
      if (!ownerToViewer && !viewerToOwner) fail(ReturnCode.PERMISSION_DENIED);
      this.send(target, {
        type: 'notify', cmd: 'notifystreamsignaling',
        args: { streamIdentifier: stream.streamIdentifier, clid: client.clid, payload },
      });
      return {};
    },
  };

  // ------------------------------------------------------------- auxiliares

  #sanitizeStreamConfig(args, base) {
    const out = {};
    out.name = 'name' in args ? (cleanString(args.name, 64) || base.name) : base.name;
    out.type = 'type' in args && Object.values(StreamType).includes(Number(args.type)) ? Number(args.type) : base.type;
    out.access = 'access' in args && [StreamAccess.PUBLIC, StreamAccess.CONTACTS_ONLY, StreamAccess.PRIVATE].includes(Number(args.access))
      ? Number(args.access) : base.access;
    out.bitrate = 'bitrate' in args ? clampInt(args.bitrate, 100_000, 50_000_000, base.bitrate) : base.bitrate;
    out.viewerLimit = 'viewerLimit' in args ? clampInt(args.viewerLimit, 0, 1000, base.viewerLimit) : base.viewerLimit;
    out.audio = 'audio' in args ? !!args.audio : base.audio;
    if (args.quality && typeof args.quality === 'object') {
      out.quality = {
        width: clampInt(args.quality.width, -1, 7680, base.quality.width),
        height: clampInt(args.quality.height, -1, 4320, base.quality.height),
        fps: clampInt(args.quality.fps, 1, 144, base.quality.fps),
      };
    } else {
      out.quality = base.quality;
    }
    return out;
  }

  #stream(streamIdentifier) {
    const stream = this.streams.get(String(streamIdentifier));
    if (!stream) fail(ReturnCode.STREAM_NOT_FOUND);
    return stream;
  }

  #ownStream(client, streamIdentifier) {
    const stream = this.#stream(streamIdentifier);
    if (stream.ownerClid !== client.clid) fail(ReturnCode.PERMISSION_DENIED);
    return stream;
  }

  #ban(stream, uid) {
    if (!uid || stream.banned.has(uid)) return;
    stream.banned.add(uid);
    this.send(stream.ownerClid, { type: 'notify', cmd: 'notifystreambanadded', args: { streamIdentifier: stream.streamIdentifier, uid } });
  }

  #removeViewer(stream, clid, reason) {
    const wasViewer = stream.viewers.delete(clid);
    const wasPending = stream.pending.delete(clid);
    if (!wasViewer && !wasPending) return;
    const message = {
      type: 'notify', cmd: 'notifystreamclientleft',
      args: { streamIdentifier: stream.streamIdentifier, clid, reason, wasPending },
    };
    this.send(stream.ownerClid, message);
    if (clid !== stream.ownerClid && this.clients.has(clid)) this.send(clid, message);
    if (wasViewer) this.#notifyAttendees(stream);
  }

  #stopStream(stream, reason) {
    this.streams.delete(stream.streamIdentifier);
    this.#broadcast({ cmd: 'notifystreamstopped', args: { streamIdentifier: stream.streamIdentifier, reason } });
  }

  #detachFromStreams(client, reason) {
    for (const stream of [...this.streams.values()]) {
      if (stream.ownerClid === client.clid) this.#stopStream(stream, LeaveReason.STREAM_STOPPED);
      else this.#removeViewer(stream, client.clid, reason);
    }
  }

  #notifyAttendees(stream) {
    const args = { streamIdentifier: stream.streamIdentifier, viewers: [...stream.viewers] };
    this.send(stream.ownerClid, { type: 'notify', cmd: 'notifystreamattendees', args });
    for (const clid of stream.viewers) this.send(clid, { type: 'notify', cmd: 'notifystreamattendees', args });
    // Contagem de espectadores é pública (aparece na árvore de canais).
    this.#broadcast({ cmd: 'notifystreamupdated', args: { stream: this.#publicStream(stream) } });
  }

  #publicClient(c) {
    return { clid: c.clid, nickname: c.nickname, uid: c.uid, cid: c.cid, inputMuted: c.inputMuted, outputMuted: c.outputMuted };
  }

  #publicStream(s) {
    return {
      streamIdentifier: s.streamIdentifier,
      ownerClid: s.ownerClid,
      cid: s.cid,
      name: s.name,
      type: s.type,
      access: s.access,
      mode: s.mode,
      bitrate: s.bitrate,
      viewerLimit: s.viewerLimit,
      audio: s.audio,
      quality: s.quality,
      paused: s.paused,
      created: s.created,
      viewerCount: s.viewers.size,
      thumbnail: s.access === StreamAccess.PUBLIC ? s.thumbnail : null,
    };
  }

  #broadcast({ cmd, args }, exceptClid = 0) {
    for (const c of this.clients.values()) {
      if (c.loggedIn && c.clid !== exceptClid) this.send(c.clid, { type: 'notify', cmd, args });
    }
  }

  #broadcastToChannel(cid, { cmd, args }, exceptClid = 0) {
    for (const c of this.clients.values()) {
      if (c.loggedIn && c.cid === cid && c.clid !== exceptClid) this.send(c.clid, { type: 'notify', cmd, args });
    }
  }
}

export { CommandError };
