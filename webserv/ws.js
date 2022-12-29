const net = require("net");
const crypto = require("crypto");
const log = x => (console.log(x), x);

function is_pong(data) {
  const bytes = new Uint8Array(data.buffer);
  const fin    = bytes[0] >> 7;
  const opcode = bytes[0] & 0b00001111;
  return opcode == 0xA;
}
function unmask(data) {
  const bytes = new Uint8Array(data.buffer);
  const fin    = bytes[0] >> 7;
  const opcode = bytes[0] & 0b00001111;
  const mask   = bytes[1] >> 7;

  if (!fin) console.log("non-fin packet found");

  let plen   = bytes[1] & 0b01111111;
  let i = 2;
  if (0) console.log('plen from byte array is', plen);
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
  const common_nbytes = i;

  const maskb = bytes.slice(i, i += 4);
  const p = bytes.slice(i, i+plen);
  if (bytes.slice(i).length != plen, 0) {
    console.log("plen no match p.length");
    console.log({ fin, opcode, mask, plen });
    console.log('p.length', bytes.slice(i).length);
    console.log('bytes[1]', bytes[1].toString(2));
    console.log("\npayload masked:");
    console.log(p);
    console.log("\npayload unmasked:");
    console.log(p.map((x, i) => x ^ maskb[i % 4]));
    console.log(".");
  }

  /* no mask bytes needed */
  const ret = new Uint8Array(common_nbytes + plen);
  /* everything but the payload and mask bytes */
  ret.set(bytes.slice(0, common_nbytes));

  /* unmask */
  ret[1] &= ~(1 << 7);
  ret.set(p.map((x, i) => x ^ maskb[i % 4]), common_nbytes);
  if (bytes.slice(i).length != plen, 0) {
    console.log('ret\'s payload says "' +
      Buffer.from(p)
            .map((x, i) => x ^ maskb[i % 4])
            .toString() +
    '"');
    console.log('ret', ret);
    console.log('bytes', bytes);
    console.log('i', i);
    console.log('bytes.slice(0, bytes.length - i - 4)',
                bytes.slice(0, bytes.length - i - 4));
  }
  return ret;
}

const clients = new Map();
let host_addr;
setInterval(() => {
  for (const [_addr, client] of clients)
    client.socket.write(new Uint8Array([
      0b10001001, /* fin: 1, opcode: ping(9) */
      0 & 0b01111111, /* no mask, no len */
    ]));
}, 1000);

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
      addr = // headers['X-Real-IP'] ??
        Math.floor(Math.random()*1e9);
      console.log(addr);
      clients.set(addr, { socket, last_hb: Date.now() });
      socket.setTimeout(1500);
      socket.on('timeout', () => (clients.delete(addr), socket.destroy()));
      socket.on('error', () => (clients.delete(addr), socket.destroy()));

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

      const kill_list = [];
      for (const [addr, client] of clients)
        if ((Date.now() - client.last_hb) > 1500)
          client.socket.destroy(),
          kill_list.push(addr);
      for (const addr of kill_list)
        clients.delete(addr);

      const host_alive = clients.has(host_addr);
      /* congratulations! you're our 0th player! */
      if (!host_alive) host_addr = addr;

      console.log({ host_addr, host_alive, client_size: clients.size, kill_list });

      const ishost_msg = JSON.stringify({ host: host_addr == addr });
      socket.write(new Uint8Array([
        0b10000001, /* fin: 1, opcode: text(1) */
        ishost_msg.length & 0b01111111, /* no mask, len */
        ...Buffer.from(ishost_msg, "utf-8")
      ]));
    },
    CONNECTED: data => {
      if (is_pong(data)) {
        clients.get(addr).last_hb = Date.now();
        return;
      }
      
      const unmasked = unmask(data);
      /* host -> everyone, everyone -> host */
      if (addr == host_addr) {
        console.log("tryna send as host?");
        for (const [addr, client] of clients)
          if (addr != host_addr)
            console.log("sending as host"),
            client.socket.write(unmasked);
      } else
        console.log("sending as client (to host)", { addr, host_addr }),
        clients.get(host_addr).socket.write(unmasked);
    }

  };
  socket.on('data', data => fsm[state](data));
});
