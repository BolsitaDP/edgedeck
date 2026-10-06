# EdgeDeck

EdgeDeck es una utilidad nativa y ligera para Windows: varias pestañas pequeñas ancladas al borde derecho del monitor principal, cada una con su propio panel desplegable.

## Compilación

Requiere Windows 10 versión 1809 o posterior, CMake 3.20+, Visual Studio 2022 Build Tools con C++ para escritorio y un Windows SDK.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Es una aplicación de subsistema Windows: no abre consola ni aparece en la barra de tareas.

### Instalar

No ejecutes EdgeDeck directamente desde `build/Release`: Windows no deja que una compilación sobrescriba un `.exe` que está en marcha, así que cada recompilación obligaba a cerrarlo antes. En su lugar:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\install.ps1 -Build
```

Compila, copia el resultado a `%LOCALAPPDATA%\EdgeDeck\bin\EdgeDeck.exe`, apunta a esa copia el acceso directo del escritorio (y la entrada de "Start with Windows", si está activada) y lo vuelve a abrir. Como la copia en marcha ya no es la salida del build, **compilar a secas con CMake no la toca**; solo cierra y reabre EdgeDeck cuando ejecutas el script, que es cuando quieres la versión nueva. La configuración vive en `%LOCALAPPDATA%\EdgeDeck\config.txt` y el script no la toca. Opciones: `-NoLaunch` (no abrirlo al terminar), `-CreateShortcut` (crear el acceso directo si no existe) y `-Configuration` (por defecto `Release`).

EdgeDeck solo admite una instancia a la vez, así que una copia abierta desde `build/Release` mientras la instalada está en marcha se cierra sola al arrancar.

## Uso

- Pasa el cursor sobre una pestaña para abrir su panel. Mientras el cursor siga sobre la pestaña o su panel, el panel permanece abierto, incluso si haces clic en algo de su interior. Se cierra solo cuando sacas el cursor (tras una breve espera).
- Cada panel tiene un botón de chincheta arriba a la derecha. Al fijarla (chincheta azul y vertical) el panel queda abierto aunque muevas el mouse fuera o hagas clic en cualquier otro sitio, hasta que vuelvas a pulsarla (chincheta hueca e inclinada).
- Clic derecho en cualquier pestaña → **Settings...** abre la ventana de configuración: tipo de widget por pestaña, posición vertical, tamaño de la pestaña y del panel, y la casilla **Start with Windows**. Los cambios se aplican al guardar y se escriben en `%LOCALAPPDATA%\EdgeDeck\config.txt`. La ventana se escala con la del monitor donde se abre (y vuelve a ajustarse sola al arrastrarla a otro con distinta escala): el diseño está en unidades lógicas de 96 DPI y se redondea por bordes, no por tamaños, para que dos controles contiguos no se solapen un píxel a escalas fraccionarias como 125 % o 175 %. Sigue el tema de los paneles (incluido el desplegable *Theme*): barra de título, fondo y controles oscuros cuando el panel es oscuro, y el aspecto normal del sistema cuando es claro o Windows está en alto contraste.
- Clic derecho → **Exit**, o `Ctrl+Alt+Q` desde cualquier lugar, cierra la aplicación.
- EdgeDeck también vive en el **área de notificación**: clic izquierdo en el icono abre Settings, clic derecho abre el menú con Settings/Exit. Es la vía de escape cuando las pestañas quedan fuera de pantalla (p. ej. desconectas el monitor) o si no tienes ninguna pestaña.

## Teclado

Los paneles nunca roban el foco de la aplicación que estés usando, así que se manejan desde el teclado mientras el puntero está sobre ellos:

| Atajo | Acción |
|---|---|
| `Ctrl+Alt+1` … `Ctrl+Alt+9` | Abre y fija la pestaña correspondiente |
| `Ctrl+Alt+Q` | Salir de EdgeDeck |
| `Ctrl+Alt+Shift+1` / `2` / `3` | Cambiar de monitores sin abrir el panel: Monitors 1 + 2 / TV only / All monitors. Solo existen mientras haya una pestaña Displays |
| Flechas | Mover el foco entre controles (izquierda/derecha también ajustan un slider) |
| Rueda del ratón | Ajustar el control enfocado |
| `Enter` / `Espacio` | Activar el control enfocado |
| `Escape` | Desfijar y cerrar el panel |

Los widgets exponen texto accesible para lectores de pantalla (`PanelWidget::AccessibleSummary` y `AccessibleControlText`).

## Inicio con Windows

La casilla **Start with Windows** (en Settings, se aplica con Save) crea o borra el valor `EdgeDeck` en `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`. Como la app no tiene consola ni ventana, arranca de forma invisible al iniciar sesión. Está pensada para que no falle en silencio:

- **"Activado" significa que de verdad arranca.** El Administrador de tareas (pestaña Inicio) no borra la entrada al desactivarla: escribe una marca en `...\Explorer\StartupApproved\Run` y la deja donde estaba. Mirar solo la entrada hacía que Settings dijera "activado" mientras Windows la ignoraba. Ahora esa casilla sale desmarcada, y marcarla y guardar la vuelve a activar de verdad (quita la marca).
- **Solo se toca el registro si cambias la casilla.** Antes cada Save reescribía la entrada con la ruta del `.exe` que estuviera corriendo, así que guardar desde una copia de pruebas se la quedaba, y volvía a escribir una entrada que el usuario había apagado.
- **Lo que falla se avisa.** El resultado se relee tras escribir, y si Windows no acepta el cambio sale una notificación en lugar de quedarse la casilla marcada y nada en Windows.
- **Se repara sola si la ruta desaparece.** Al arrancar, si la entrada apunta a un fichero que ya no existe (se movió el `.exe`, se limpió una carpeta de compilación), se reescribe apuntando a la copia instalada (`%LOCALAPPDATA%\EdgeDeck\bin`), o si no existe a la que está corriendo. No crea una entrada que no pediste, no toca la marca del Administrador de tareas (que quiera arrancar o no es decisión del usuario) y no se apropia de una entrada que apunta a **otra copia que sí existe**: así una copia de desarrollo convive con la instalada.
- Al activarla se prefiere la copia instalada por `scripts/install.ps1`, porque su ruta no cambia al recompilar.

Lo que esto no hace: si EdgeDeck se cierra por un fallo, no se reinicia hasta el siguiente inicio de sesión (la entrada Run no es un servicio ni un vigilante).

## Widgets

- **Quick Actions**: abrir Bloc de notas, abrir Calculadora, mostrar/ocultar el escritorio.
- **Media (auto-detect)**: una fila por cada app con sesión de reproducción activa en Windows (Spotify, una pestaña de Chrome/Edge, VLC, etc). Cada fila muestra el badge de la app, el nombre, la pista, una barra de progreso y los botones Anterior / Reproducir-Pausa / Siguiente.
- **Volume**: una fila por cada dispositivo de salida de audio, con slider de volumen y botón de silencio. Arrastrar un slider silenciado lo reactiva, que es lo que se espera del gesto.
- **Brightness (monitors)**: una fila por monitor con su nombre real y un slider de brillo (clic o arrastre; se aplica al soltar). Usa DDC/CI (API de configuración de monitores de Windows), así que funciona con monitores externos que lo tengan activado; los que no lo soportan muestran "Brightness control not available". Los paneles integrados de portátiles no usan DDC/CI y por ahora no se controlan.
- **Displays (switch monitors)**: tres botones para decidir qué monitores están encendidos: **Monitors 1 + 2** (los de escritorio), **TV only** y **All monitors**. El botón que corresponde a lo que está encendido en ese momento aparece marcado como *Active*. Usa la API de configuración de pantallas de Windows (CCD, la misma de Configuración > Pantalla), así que puede volver a encender un monitor apagado y Windows restaura la disposición que ya tenías. Al cambiar, el monitor principal puede ser otro (con "TV only" lo es la TV) y las pestañas se vuelven a acoplar al borde derecho del que haya quedado como principal; un panel abierto y no fijado se cierra.

  La TV se detecta sola como el panel más grande, lo cual acierta con una TV 4K junto a monitores de escritorio. Si no acierta, se fija a mano en `config.txt`: `tvMonitor=SAM7A08` dentro de `[settings]`. El valor es el código de fabricante y producto del EDID del monitor, que aparece en el Administrador de dispositivos > Monitores > Detalles > Id. de hardware (`MONITOR\SAM7A08`). Si la TV está apagada o desconectada, los botones que dependen de ella salen deshabilitados con "TV not connected".

  Los tres botones también tienen atajo global, `Ctrl+Alt+Shift+1`, `2` y `3`, que cambian la disposición sin abrir el panel. Se registran solo mientras exista una pestaña Displays (quien no use el widget no reserva esas teclas) y se actualizan al guardar en Settings. Un atajo siempre vuelve a leer los monitores antes de actuar, porque el panel puede llevar horas sin abrirse y el estado que recuerda estar caduco (Win+P, un monitor enchufado después); y la orden queda ligada a esa lectura concreta, de modo que una respuesta descartada no puede ejecutarla más tarde. Si otro programa ya tiene esa combinación, solo se anota en el log.
- **Lyrics**: muestra la letra sincronizada de lo que sea que esté sonando (no solo Spotify - sigue al mismo "reproductor activo" que ya usa Windows para el resto del sistema). Las letras vienen de [LRCLIB](https://lrclib.net), una base de datos pública y gratuita hecha para esto, sin login ni API key.

El valor por defecto trae una pestaña de Quick Actions, Media, Volume y Brightness; Lyrics y Displays se agregan desde Settings → Add si los quieres usar.

Las letras que se obtienen se guardan en caché local (`%LOCALAPPDATA%\EdgeDeck\lyrics_cache\`), así que no se vuelve a pedir por red la próxima vez que suene la misma canción. Los "no hay letra" también se cachean (con 7 días de caducidad), para no golpear un servicio público gratuito cada vez que se abre el panel. Al ser una base comunitaria, alguna canción muy nueva o poco común puede no tener letra sincronizada disponible todavía.

## Apariencia

El panel sigue el tema de Windows usando **los mismos colores que usa el propio sistema**: los grises de la rampa oscura/elevada (`#202020`, `#1A1A1A`, `#333333`), la neutra de respaldo de Mica en claro (`#F3F3F3`), y el **acento que haya elegido el usuario**, leído de `Themes\Personalize\AccentColor`.

En Ajustes hay un desplegable **Theme** con *Follow Windows* (por defecto), *Always dark* y *Always light*, para cuando el ajuste del sistema y el panel que prefieres no coinciden — sistema claro con panel oscuro, o al revés. El alto contraste tiene prioridad sobre las dos opciones.

El acento se ajusta hacia blanco o negro hasta cumplir 3:1 de contraste contra el fondo del panel. Sin eso, un acento azul oscuro elegido por el usuario sería invisible como relleno de un slider sobre un panel oscuro; es la misma idea que aplicar Windows al modo oscuro, calculada en vez de tomada de un segundo valor del registro que no siempre existe.

Cuando el **alto contraste** está activo se abandonan estos valores y se toman directamente los colores de ventana, texto y botón del usuario, que es justo para lo que existe ese modo.

Cambiar el tema no reinicia la app: la paleta se recalcula al recibir `WM_SETTINGCHANGE` / `WM_THEMECHANGED` / `WM_SYSCOLORCHANGE`, y los pinceles que llevan el color hornearado se sueltan al detectarla. Antes esta lectura se hacía **en cada pintado** —una lectura del registro más un `SystemParametersInfo`, unas quince veces por frame de animación— y nunca se comprobaba el resultado, así que un fallo silencioso se traducía en caer siempre a la paleta clara.

## Notas técnicas

**Nada de polling en reposo.** No hay render loop ni sondeo del ratón: el hover usa `TrackMouseEvent` / `WM_MOUSELEAVE`. Solo hay temporizadores en cuatro casos concretos, y ninguno corre con los paneles cerrados:

- la animación de apertura/cierre (15 ms mientras dura),
- la breve espera antes de cerrar (320 ms),
- un muestreo de 500 ms **solo mientras un panel está visible** y solo para widgets que lo piden. Existe únicamente para la posición de reproducción dentro de una pista, que es lo único que Windows no notifica por evento; Lyrics y Media lo usan, el resto no lo activa,
- los 6 s que un aviso de error permanece en la cabecera (una sola vez, y solo mientras hay un aviso).

**Avisos de error sin ventanas modales.** Cuando algo falla (una acción rápida, un control de Media, un cambio de monitores) el widget lo informa con `PanelWidget::ReportProblem`, que envía un mensaje a la ventana de su pestaña y es `Tab` quien decide cómo mostrarlo. Con el panel a la vista, el aviso sustituye al título de la cabecera durante 6 s, en ámbar (o en el color de texto del usuario en alto contraste), con envoltura a dos líneas y recorte, sin cambiar el tamaño del panel ni tapar el contenido; sin panel a la vista (un atajo, un panel que ya se cerró) sale como notificación de Windows desde el icono de la bandeja. Antes eran `MessageBox` modales: congelaban lo que el usuario estuviera haciendo, y el del atajo de salida ocupado salía antes del bucle de mensajes, de modo que la aplicación quedaba invisible detrás de un diálogo que nadie veía. Ese aviso de arranque espera ahora a que exista el icono de la bandeja. Los tres diálogos de `main.cpp` (sin candado de instancia, sin COM, sin ventanas) se mantienen: son fallos fatales tras los que el proceso termina y no queda dónde mostrar nada.

**Cambios por evento, no por sondeo.** Media se suscribe a los eventos de
`GlobalSystemMediaTransportControlsSessionManager` (`CurrentSessionChanged`,
`SessionsChanged`) y de cada sesión (`PlaybackInfoChanged`,
`MediaPropertiesChanged`), de modo que empezar a reproducir, pausar o saltar
actualiza el panel abierto al instante. Lo mismo hace Lyrics para re-buscar la
letra cuando cambia de pista. No hay hooks globales de ratón ni de teclado, salvo
el atajo de salida y los de apertura registrados con `RegisterHotKey`.

**Propiedad de los resultados asíncronos.** Todo trabajo en segundo plano
(WinRT, DDC/CI, HTTPS) devuelve su resultado con un sobre único
(`AsyncResult.h`) que lleva un `requestId` y una "generación de lista". El
receptor siempre lo libera, lo haya pedido o no, y lo descarta si no coincide con
lo que tiene en vuelo. Eso elimina de raíz las tres formas en que esto se rompe:
fuga cuando la pestaña se destruye con un trabajo en curso, fuga cuando el
mensaje llega a un HWND reciclado, y datos rancios cuando un refresco sustituye a
otro.

**Un slider de brillo** muestra el valor en vivo mientras lo arrastras, pero la
escritura DDC/CI al monitor se hace una sola vez, al soltar (varios monitores
guardan el brillo en memoria no volátil, así que no conviene escribir de forma
continua). Si el monitor rechaza la escritura, la fila vuelve al último valor que
el hardware confirmó en lugar de seguir mostrando un brillo que no es real. En
Core Audio no hace falta aplazar nada: el volumen es una llamada en proceso, así
que el slider escribe en cada movimiento y el panel se relee mientras está
visible para seguir las teclas de volumen de otra app.

**Red.** Las peticiones a LRCLIB usan una sesión WinHTTP compartida con keep-alive
y timeouts explícitos (5 s resolver/conectar/enviar, 8 s recibir): los valores por
omisión de WinHTTP son de ~60 s, suficiente para que un mal día de red deje un hilo del
pool bloqueado un minuto, y cada apertura de panel puede lanzar otro. La respuesta
tiene un tope de 4 MB.

**Transformaciones de Direct2D.** `SetTransform` *reemplaza* la matriz, no la
compone, y el chrome del panel vive en una traslación que se instala antes de
delegar en el widget. Por eso `Renderer` compone toda transformación local
*sobre* la vigente (`ScopedTransform::Compose`) en vez de fijarla: hacerlo al revés
manda los iconos fuera del panel, y terminar con `SetTransform(Identity)` en vez de
restaurar deja el resto del contenido desplazado exactamente el alto del chrome.
Parece una sutileza, pero produce un fallo de layout que parece un error
aritmético en el widget.

**Cambiar de monitores.** Encender un monitor es `SetDisplayConfig`, que Windows retiene mientras los paneles se resincronizan (varios segundos con una TV en la cadena), así que corre en el pool de hilos y avisa con un único mensaje, igual que el resto de trabajos en segundo plano. Apagar monitores reescribe la disposición vigente sin los que sobran, de modo que los que se quedan no se mueven; encender uno le pasa a Windows solo las rutas, sin modos, y Windows rellena resolución y posición con lo que tiene guardado para esa combinación exacta de monitores. Los monitores se identifican por el código EDID (`SAM7A08`) y no por el LUID del adaptador o el id del destino, que cambian al reiniciar o recargar el driver. El planificador (`DisplayTopology`) no tiene ventanas y se prueba con topologías hechas a mano; la validación contra hardware se hace con `SDC_VALIDATE`, que comprueba la configuración sin aplicarla.

**Esquinas redondeadas.** La forma de cada ventana es su propio canal alfa: Direct2D
dibuja en un DIB de 32 bits premultiplicado mediante un `ID2D1DCRenderTarget` **de
software**, y `UpdateLayeredWindow` lo entrega al compositor (`LayeredTarget`). Así
las esquinas quedan suavizadas a cualquier escala; el recorte anterior con
`SetWindowRgn` es una máscara de 1 bit y salía escalonado desde 125%. El pintado
pone el fondo transparente y rellena el rectángulo redondeado encima: limpiar con el
color opaco del panel daría alfa 255 en todos los píxeles y las esquinas nunca serían
transparentes. La transparencia de todo el panel (246/255) va en el `BLENDFUNCTION`.

Un detalle que costó descubrirlo: la conclusión de un intento anterior ("toda
superficie D2D sin dispositivo DXGI pierde el alfa") era cierta para
`ID2D1HwndRenderTarget`, que compone a través de GDI, pero no para un render target de
DC enlazado a un DIB. Por eso no hace falta Direct3D, ni leer de vuelta de memoria de
vídeo, ni cargar el driver de la GPU para una ventana de 300x200. Medido con dos
pestañas, frente al camino de región: memoria de trabajo 19,6 MB en lugar de 34,5, memoria
privada 5 MB en lugar de 42, 13 hilos en lugar de 24 y 200 handles en lugar de 334, con la
CPU en el hover igual o menor. Si el render target de DC no se puede crear, cada
ventana cae al camino anterior (región + `SetLayeredWindowAttributes`), que se decide
una vez en `AttachToWindow` porque dicta también cómo se configura la ventana: una
ventana en capas a la que nadie llama ni a `UpdateLayeredWindow` ni a
`SetLayeredWindowAttributes` no se muestra. El texto es en escala de grises, no
ClearType, porque una superficie con alfa no sabe qué hay detrás.

**Registro.** Los fallos que antes eran invisibles (una superficie D2D que no se
puede crear, un `config.txt` que no se puede escribir, una pestaña que no se puede
construir) se anotan en `%LOCALAPPDATA%\EdgeDeck\edgedeck.log`, con rotura a
`.old` al pasar de 512 KB.

**Pruebas.** `tests/` cubre lo que no tiene ventanas: el parser LRC (fracciones,
marcas repetidas, tags de metadatos, `offset`, orden), el fichero de config
(ida y vuelta, valores fuera de rango, números mal formados, claves desconocidas), la
lógica de la pestaña Displays (qué monitor es la TV, qué botón está activo o disponible, y
la planificación del cambio: el origen del escritorio, los índices de modos y la asignación de fuentes),
el envío de avisos de error entre widget y pestaña (texto intacto, sin fugas con un destinatario nulo o ya destruido),
la lógica del autoarranque (entrada desactivada en el Administrador de tareas, reparación de la ruta, qué entradas se respetan; contra una clave de registro de pruebas, nunca las entradas reales)
y la superficie con alfa por píxel (esquinas transparentes, borde suavizado, colores premultiplicados,
escala DPI), que se comprueba mirando los bytes del DIB y no necesita ventana.
Se ejecutan con `ctest` en CI (`.github/workflows/build.yml`). El resto son
widgets y ventanas, y no hay forma de ejercitarlos sin sesión de escritorio.
