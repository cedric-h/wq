// vim: sw=2 ts=2 expandtab smartindent
const { performance } = require('perf_hooks');

/* TODO: get addr from host code */
    /* - how to go from socket handling to this function? */
    /* - probably expose buffer to outside */
/* env_send */
    /* - index into clients map? call send? */
    /* - TODO: message encoding! (just parsing code in reverse?) */

/* milestones: send basic binary message to client and print it */

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

const setTick = (ms, logic) => {
  const start = performance.now();
  let tick = 0;
  (function step() {
    setTimeout(step, 0);

    while ((performance.now() - start) / ms > tick)
      logic(tick++);
  })();
}

const to_host = [];
const clients = {};

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
      const msg = new Uint8Array(len + 2);
      msg.set(src, 2);
      msg[0] =       0b10000010; /* fin, binary */
      msg[1] = len & 0b01111111; /* len, no mask */
      if (len > 126) console.log("msg too big");

      const addr = new Uint8Array(mem, addr_ptr, 4).join('.');
      clients[addr].write(Buffer.from(msg));
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

  setTick(1000/60, () => {
    instance.exports.wq_update();
  });
})();

const net = require("net");
const crypto = require("crypto");
const log = x => (console.log(x), x);

const server = new net.Server();
console.log('listening on', 8080, '127.0.0.1');
server.listen(8080, '127.0.0.1', console.log);
server.on('connection', socket => {
  console.log("bruh");

  let addr;
  let state = "HANDSHAKING";
  const fsm = {
    HANDSHAKING: data => {
      console.log("Ahoy!");
      const msg = data.toString();
      console.log(msg);

      const headers = 
        Object.fromEntries(
          msg
            .split('\n')
            .slice(1)
            .map(x => x.split(': ').map(str => str.trim()))
        );
      const ws_key = headers['Sec-WebSocket-Key'];
      addr = headers['X-Real-IP'];
      clients[addr] = socket;

      console.log(addr, '"' + ws_key + '"');

      socket.write(log(
        "HTTP/1.1 101 Switching Protocols\r\n" +
        "Upgrade: websocket\r\n" +
        "Connection: Upgrade\r\n" +
        "Sec-WebSocket-Accept: " +
          crypto
            .createHash('sha1')
            .update(ws_key)
            .update("258EAFA5-E914-47DA-95CA-C5AB0DC85B11")
            .digest('base64') + "\r\n" + "\r\n"
      ));
      state = "CONNECTED";
    },
    CONNECTED: data => {
      const bytes = new Uint8Array(data.buffer);
      const fin    = bytes[0] >> 7;
      const opcode = bytes[0] & 0b00001111;
      const mask   = bytes[1] >> 7;

      let plen   = bytes[1] & 0b01111111;
      let i = 2;
      if (plen == 127) i += 8, plen = (bytes[9]      ) |
                                      (bytes[8] <<  8) |
                                      (bytes[7] << 16) |
                                      (bytes[6] << 24) |
                                      (bytes[5] << 32) |
                                      (bytes[4] << 40) |
                                      (bytes[3] << 48) |
                                      (bytes[2] << 54);
      else if (plen == 126) i += 2, plen = (bytes[3]      ) |
                                           (bytes[2] <<  8);

      const maskb = [bytes[i++], bytes[i++], bytes[i++], bytes[i++]];
      const p = bytes.slice(i);
      if (p.length != plen) {
        console.log("plen no match p.length");
        console.log({ fin, opcode, mask, plen });
        console.log('p.length', p.length);
        console.log('bytes[1]', bytes[1].toString(2));
        console.log("aherm,");
        console.log(p.map((x, i) => x ^ maskb[i % 4]));
        console.log(".");
      }
      to_host.push({
        addr,
        data: p.map((x, i) => x ^ maskb[i % 4]),
      });
    }

  };
  socket.on('data', data => fsm[state](data));
});
