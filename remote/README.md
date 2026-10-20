# RUMOR remoto

Hub Socket.IO y página para controlar RUMOR desde otro dispositivo.

El navegador habla Socket.IO. La app JUCE abre un WebSocket plano en `/device`. El servidor reenvía el estado, el debug del serial y los comandos. El puerto serial sigue en la máquina donde corre RUMOR.

## Local

```bash
cd remote
npm install
npm start
```

Por defecto escucha en el puerto 5000. En macOS ese puerto lo suele ocupar AirPlay (Control Center). Chrome además rechaza el puerto 6000 (`ERR_UNSAFE_PORT`).

```bash
PORT=8080 npm start
```

Abrí `http://<esta-máquina>:8080`. En RUMOR, pestaña **serial**, pegá `ws://<esta-máquina>:8080` y tocá **vincular**.

`HOST` (por defecto `0.0.0.0`) es opcional. Si definís `RUMOR_TOKEN`, la URL del plugin y la de la página tienen que llevar el mismo `?token=`.

El plugin todavía no habla `wss://`.

## Render

Hay un blueprint en `render.yaml`, en la raíz del repo.

1. En Render: **New → Blueprint**, y apuntá a este repositorio. También se puede crear un Web Service a mano con directorio raíz `remote`, build `npm ci` y start `npm start`.
2. El health check es `/health`. Render inyecta `PORT`; no hace falta definirlo.
3. La página queda en `https://<servicio>.onrender.com`.
4. El plugin no puede usar esa URL: Render solo expone HTTPS y el cliente JUCE todavía no abre `wss://`. Para **vincular**, el hub tiene que ser alcanzable con `ws://` (la misma red, o un host sin TLS).

`RUMOR_TOKEN`, si lo usás, se carga en el panel de entorno de Render. No lo pongas en el repo.
