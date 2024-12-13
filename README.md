### ReverseProxy (<a href="https://youtu.be/BcuAmFdyb3o" targe="_blank">Demo en YouTube</a>)
<p>Proxy inversa simple <i>(Parte de otro proyecto mas grande)</i>.

<p>Hace uso de un solo socket entre el cliente y el servidor durante toda la interaccion.<br><br>
El servidor escucha por dos puertos:
</p>

Puerto | Description
---- | ----
6666 | Puerto para conexiones locales (ej. navegador)
7777 | Puerto para enviar datos al proxy remoto

<p>Por cada conexion local entrante se crea un thread el cual asigna un id unico a cada conexion generado aletoriamente para identificar a donde reenviar los datos en ambos puntos.<br>
Asi mismo en el lado del cliente remoto (proxy) se crea un thread por cada peticion
</p>

### Estructura del paquete
<p>Cada paquete que se envia entre ambos puntos va de la siguiente manera:</p>


T  | D | ID
------- | ----- | ----- 
4 BYTES | ... | 4 BYTES 
Tama&ntilde;o del buffer + 4 (ID de conexion) | BUFFER | ID de conexion

Demo
<img src="./imagenes/simple_demo.jpg"></p>

