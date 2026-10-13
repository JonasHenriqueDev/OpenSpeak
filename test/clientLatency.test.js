import test from 'node:test';
import assert from 'node:assert/strict';
import { ConnectionHandler } from '../public/js/core/ConnectionHandler.js';

test('web latency notifications update only measured clients and own server ping', () => {
  const handler = new ConnectionHandler();
  handler.applyView({clid:1,server:{name:'test'},channels:[],streams:[],clients:[{clid:1,nickname:'a'},{clid:2,nickname:'b'}]});
  assert.equal(handler.serverPingMs,null);
  handler.connection.emit('notifyclientlatency',{clid:2,pingMs:75,pingUpdatedAt:1});
  assert.equal(handler.clients.get(2).pingMs,75);
  assert.equal(handler.serverPingMs,null);
  handler.connection.emit('notifyclientlatency',{clid:1,pingMs:25,pingUpdatedAt:2});
  assert.equal(handler.serverPingMs,25);
  handler.connection.emit('notifyclientlatency',{clid:1,pingMs:null,pingUpdatedAt:3});
  assert.equal(handler.serverPingMs,null);
  handler.connection.emit('notifyclientlatency',{clid:99,pingMs:20,pingUpdatedAt:4});
  assert.equal(handler.clients.has(99),false);
  assert.equal(handler.clients.get(2).nickname,'b');
});
