// vim: sw=2 ts=2 expandtab smartindent ft=javascript
const { WebSocketServer } = require('ws');
const { PeerConnection } = require('node-datachannel');

const discordlog = msg => {
  console.log(msg);
  return;
  require("child_process").exec(
    'curl -i ' +
      '-H "Accept: application/json" ' +
      '-H "Content-Type:application/json" ' +
      '-X POST --data "{\\"content\\": \\"' + msg + '\\"}" ' +
    'https://discord.com/api/webhooks/1055842363928477757/Czl79kYmXIPL7Du0M1G1VZ2FkU-K4rcPjAWkxwxqlQspe9l-Ho7vBxM77ajnfgTWj70j'
  );
}

const wss = new WebSocketServer({ port: 8080 });
console.log("listenin on 8080 yall");

const to_host = [];
const clients = {};

wss.on('connection', function connection(ws) {
  const rbyte = () => Math.floor(Math.random() * 256);
  const addr = [...Array(4)].map(rbyte).join('.');

  let rtc = new PeerConnection(addr, {
    iceServers: ["stun:stun.l.google.com:19302"]
  });
  
  const send = msg => ws.send(JSON.stringify(msg));
  rtc.onLocalDescription((sdp, type) => 
                    send({sdp, type}));
  rtc.onLocalCandidate((candidate, sdpMid) =>
                  send({candidate, sdpMid}));

  const dc1 = rtc.createDataChannel("dc");
  dc1.onOpen(() => clients[addr] = dc1);
  dc1.onMessage(data => to_host.unshift({ addr, data }));

  ws.on('message', function message(data) {
    const json = JSON.parse(data);
    if (json.sdp)
      rtc.setRemoteDescription(json.sdp, json.type);
    if (json.candidate)
      rtc.setRemoteCandidate(candidate);
  });
});

const setTick = (ms, logic) => {
  const start = performance.now();
  let tick = 0;
  (function step() {
    setTimeout(step, 0);

    while ((performance.now() - start) / ms > tick)
      logic(tick++);
  })();
}

const game = (async () => {
  const ts_start = performance.now();
  let instance;

  const env = {
    print_f: console.log,
    print: (ptr, len) => discordlog(
      new TextDecoder().decode(new Uint8Array(
        instance.exports.memory.buffer, ptr, len))
    ),
    fmodf: (f, n) => f % n,
    cosf: Math.cos,
    sinf: Math.sin,
    sqrtf: Math.sqrt,
    floorf: Math.floor,
    ceilf: Math.ceil,
    atan2f: Math.atan2,

    env_host_recv: (addr_ptr, buf, len) => {
      if (to_host.length == 0) return 0;

      const { addr, data } = to_host.pop();
      const mem = instance.exports.memory.buffer;
      const dst = new Uint8Array(mem, buf, len);
      dst.set(data);

      const bytes = addr.split('.');
      new Uint8Array(mem, addr_ptr)[0] = parseInt(bytes[0]);
      new Uint8Array(mem, addr_ptr)[1] = parseInt(bytes[1]);
      new Uint8Array(mem, addr_ptr)[2] = parseInt(bytes[2]);
      new Uint8Array(mem, addr_ptr)[3] = parseInt(bytes[3]);

      return 1;
    },

    env_send: (addr_ptr, buf, len) => {
      const mem = instance.exports.memory.buffer;
      
      const src = new Uint8Array(mem, buf, len);
      const msg = new Uint8Array(len);
      msg.set(src);

      const addr = new Uint8Array(mem, addr_ptr, 4).join('.');
      try { clients[addr].sendMessageBinary(Buffer.from(msg)); }
      catch(e) { delete clients[addr]; }
    },

    /* no need to send to ourselves */
    env_send_to_host: (buf, len) => {},
    /* no need to receive messages as client on server */
    env_clnt_recv: (buf, len) => { return 0; },

    env_ts: () => (performance.now() - ts_start) / 1000,

    /* don't need these yet :) */
    env_trace_begin: () => {},
    env_trace_end: () => {},
    env_dbg_sys_run: () => {},
    env_dbg_dylib_reload: () => {},
  };

  const wasmpath = "/var/www/html/build/main.wasm";
  ({ instance } = await WebAssembly.instantiate(
    require("fs").readFileSync(wasmpath),
    { env }
  ));
  console.log("successfully compiled " + wasmpath);

  instance.exports.init(0, 0, 1);

  setTick(1000/60, () => instance.exports.wq_update());
})();
