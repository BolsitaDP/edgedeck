# EdgeDeck

EdgeDeck es una utilidad nativa y ligera para Windows: varias pestañas pequeñas ancladas al borde derecho del monitor principal, cada una con su propio panel desplegable.

## Compilación

Requiere Windows 10 versión 1809 o posterior, CMake 3.20+, Visual Studio 2022 Build Tools con C++ para escritorio y un Windows SDK.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Ejecuta `build/Release/EdgeDeck.exe`. Es una aplicación de subsistema Windows: no abre consola ni aparece en la barra de tareas.

## Uso

- Pasa el cursor sobre una pestaña para abrir su panel. Mientras el cursor siga sobre la pestaña o su panel, el panel permanece abierto, incluso si haces clic en algo de su interior. Se cierra solo cuando sacas el cursor (tras una breve espera).
- Cada panel tiene un botón de chincheta arriba a la derecha. Al fijarla (chincheta azul y vertical) el panel queda abierto aunque muevas el mouse fuera o hagas clic en cualquier otro sitio, hasta que vuelvas a pulsarla (chincheta hueca e inclinada).
- Clic derecho en cualquier pestaña → **Settings...** abre la ventana de configuración: tipo de widget por pestaña, posición vertical, tamaño de la pestaña y del panel, y la casilla **Start with Windows**. Los cambios se aplican al guardar y se escriben en `%LOCALAPPDATA%\EdgeDeck\config.txt`.
- Clic derecho → **Exit**, o `Ctrl+Alt+Q` desde cualquier lugar, cierra la aplicación.
- EdgeDeck también vive en el **área de notificación**: clic izquierdo en el icono abre Settings, clic derecho abre el menú con Settings/Exit. Es la vía de escape cuando las pestañas quedan fuera de pantalla (p. ej. desconectas el monitor) o si no tienes ninguna pestaña.

## Teclado

Los paneles nunca roban el foco de la aplicación que estés usando, así que se manejan desde el teclado mientras el puntero está sobre ellos:

| Atajo | Acción |
|---|---|
| `Ctrl+Alt+1` … `Ctrl+Alt+9` | Abre y fija la pestaña correspondiente |
| `Ctrl+Alt+Q` | Salir de EdgeDeck |
| Flechas | Mover el foco entre controles (izquierda/derecha también ajustan un slider) |
| Rueda del ratón | Ajustar el control enfocado |
| `Enter` / `Espacio` | Activar el control enfocado |
| `Escape` | Desfijar y cerrar el panel |

Los widgets exponen texto accesible para lectores de pantalla (`PanelWidget::AccessibleSummary` y `AccessibleControlText`).

## Inicio con Windows

La casilla **Start with Windows** (en Settings, se aplica con Save) crea o borra el valor `EdgeDeck` en `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` con la ruta del ejecutable actual. Como la app no tiene consola ni ventana, arranca de forma invisible al iniciar sesión. Si mueves el `.exe`, desmarca y vuelve a marcar la casilla para actualizar la ruta.

## Widgets

- **Quick Actions**: abrir Bloc de notas, abrir Calculadora, mostrar/ocultar el escritorio.
- **Media (auto-detect)**: una fila por cada app con sesión de reproducción activa en Windows (Spotify, una pestaña de Chrome/Edge, VLC, etc). Cada fila muestra el badge de la app, el nombre, la pista, una barra de progreso y los botones Anterior / Reproducir-Pausa / Siguiente.
- **Volume**: una fila por cada dispositivo de salida de audio, con slider de volumen y botón de silencio. Arrastrar un slider silenciado lo reactiva, que es lo que se espera del gesto.
- **Brightness (monitors)**: una fila por monitor con su nombre real y un slider de brillo (clic o arrastre; se aplica al soltar). Usa DDC/CI (API de configuración de monitores de Windows), así que funciona con monitores externos que lo tengan activado; los que no lo soportan muestran "Brightness control not available". Los paneles integrados de portátiles no usan DDC/CI y por ahora no se controlan.
- **Lyrics**: muestra la letra sincronizada de lo que sea que esté sonando (no solo Spotify - sigue al mismo "reproductor activo" que ya usa Windows para el resto del sistema). Las letras vienen de [LRCLIB](https://lrclib.net), una base de datos pública y gratuita hecha para esto, sin login ni API key.

El valor por defecto trae una pestaña de Quick Actions, Media, Volume y Brightness; Lyrics se agrega desde Settings → Add si la quieres usar.

Las letras que se obtienen se guardan en caché local (`%LOCALAPPDATA%\EdgeDeck\lyrics_cache\`), así que no se vuelve a pedir por red la próxima vez que suene la misma canción. Los "no hay letra" también se cachean (con 7 días de caducidad), para no golpear un servicio público gratuito cada vez que se abre el panel. Al ser una base comunitaria, alguna canción muy nueva o poco común puede no tener letra sincronizada disponible todavía.

## Apariencia

El panel sigue el tema de Windows usando **los mismos colores que usa el propio sistema**: los grises de la rampa oscura/elevada (`#202020`, `#1A1A1A`, `#333333`), la neutra de respaldo de Mica en claro (`#F3F3F3`), y el **acento que haya elegido el usuario**, leído de `Themes\Personalize\AccentColor`.

El acento se ajusta hacia blanco o negro hasta cumplir 3:1 de contraste contra el fondo del panel. Sin eso, un acento azul oscuro elegido por el usuario sería invisible como relleno de un slider sobre un panel oscuro; es la misma idea que aplicar Windows al modo oscuro, calculada en vez de tomada de un segundo valor del registro que no siempre existe.

Cuando el **alto contraste** está activo se abandonan estos valores y se toman directamente los colores de ventana, texto y botón del usuario, que es justo para lo que existe ese modo.

Cambiar el tema no reinicia la app: la paleta se recalcula al recibir `WM_SETTINGCHANGE` / `WM_THEMECHANGED` / `WM_SYSCOLORCHANGE`, y los pinceles que llevan el color hornearado se sueltan al detectarla. Antes esta lectura se hacía **en cada pintado** —una lectura del registro más un `SystemParametersInfo`, unas quince veces por frame de animación— y nunca se comprobaba el resultado, así que un fallo silencioso se traducía en caer siempre a la paleta clara.

## Notas técnicas

**Nada de polling en reposo.** No hay render loop ni sondeo del ratón: el hover usa `TrackMouseEvent` / `WM_MOUSELEAVE`. Solo hay temporizadores en tres casos concretos, y ninguno corre con los paneles cerrados:

- la animación de apertura/cierre (15 ms mientras dura),
- la breve espera antes de cerrar (320 ms),
- un muestreo de 500 ms **solo mientras un panel está visible** y solo para widgets que lo piden. Existe únicamente para la posición de reproducción dentro de una pista, que es lo único que Windows no notifica por evento; Lyrics y Media lo usan, el resto no lo activa.

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

**Esquinas redondeadas.** Se siguen recortando con `SetWindowRgn`, que es una
máscara de 1 bit y por tanto escalonada a partir de 125% de escalado. Hacerlas
correctas exige alfa por píxel, y toda superficie D2D disponible sin un dispositivo
DXGI (`ID2D1HwndRenderTarget` e `ID2D1DCRenderTarget` incluidos) presenta a través
de GDI, que descarta el canal alfa: se intentó, componiendo con
`UpdateLayeredWindow`, y la ventana se componía sin mostrar nada. Solucionarlo de
verdad pasa a `ID2D1DeviceContext` sobre `ID2D1Device1` con volcado manual del
bitmap, que es un cambio bastante mayor que la calidad de las esquinas.

**Registro.** Los fallos que antes eran invisibles (una superficie D2D que no se
puede crear, un `config.txt` que no se puede escribir, una pestaña que no se puede
construir) se anotan en `%LOCALAPPDATA%\EdgeDeck\edgedeck.log`, con rotura a
`.old` al pasar de 512 KB.

**Pruebas.** `tests/` cubre lo que no tiene ventanas: el parser LRC (fracciones,
marcas repetidas, tags de metadatos, `offset`, orden) y el fichero de config
(ida y vuelta, valores fuera de rango, números mal formados, claves desconocidas).
Se ejecutan con `ctest` en CI (`.github/workflows/build.yml`). El resto son
widgets y ventanas, y no hay forma de ejercitarlos sin sesión de escritorio.
