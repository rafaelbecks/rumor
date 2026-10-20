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

Abrí `http://<esta-máquina>:8080`. En RUMOR, pestaña **serial**, la URL ya viene con `https://rumor-remote.onrender.com`. Para un hub en la misma red, cambiala por `ws://<esta-máquina>:8080` y tocá **vincular**.

`https://` se abre como `wss://` y `http://` como `ws://`. `HOST` (por defecto `0.0.0.0`) es opcional. Si definís `RUMOR_TOKEN`, la URL del plugin y la de la página tienen que llevar el mismo `?token=`.

## Render

Hay un blueprint en `render.yaml`, en la raíz del repo.

1. En Render: **New → Blueprint**, y apuntá a este repositorio. También se puede crear un Web Service a mano con directorio raíz `remote`, build `npm ci` y start `npm start`.
2. El health check es `/health`. Render inyecta `PORT`; no hace falta definirlo.
3. La página queda en `https://rumor-remote.onrender.com` (o la URL del servicio).
4. Esa dirección es la que aparece por defecto en el plugin. Tocá **vincular**. Un hub local sigue usando `ws://`.

`RUMOR_TOKEN`, si lo usás, se carga en el panel de entorno de Render. No lo pongas en el repo.
