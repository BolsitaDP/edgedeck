# EdgeDeck

EdgeDeck es una utilidad nativa y ligera para Windows: varias pestañas pequeñas ancladas al borde derecho del monitor principal, cada una con su propio panel desplegable.

## Compilación

Requiere Windows 10 versión 1809 o posterior, CMake 3.20+, Visual Studio 2022 Build Tools con C++ para escritorio y un Windows SDK.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Ejecuta `build/Release/EdgeDeck.exe`. Es una aplicación de subsistema Windows: no abre consola ni aparece en la barra de tareas.

## Uso

- Pasa el cursor sobre una pestaña para abrir su panel. Mientras el cursor siga sobre la pestaña o su panel, el panel permanece abierto, incluso si haces clic en algo de su interior. Se cierra solo cuando sacas el cursor (tras una breve espera).
- Cada panel tiene un botón de chincheta arriba a la derecha. Al fijarla (chincheta azul y vertical) el panel queda abierto aunque muevas el mouse fuera o hagas clic en cualquier otro sitio, hasta que vuelvas a pulsarla (chincheta hueca e inclinada).
- Clic derecho en cualquier pestaña → **Settings...** abre la ventana de configuración: tipo de widget por pestaña, posición vertical, tamaño de la pestaña y del panel, y la casilla **Start with Windows**. Los cambios se aplican al guardar y se escriben en `%LOCALAPPDATA%\EdgeDeck\config.txt`.
- Clic derecho → **Exit**, o `Ctrl+Shift+Alt+Q` desde cualquier lugar, cierra la aplicación.

## Inicio con Windows

La casilla **Start with Windows** (en Settings, se aplica con Save) crea o borra el valor `EdgeDeck` en `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` con la ruta del ejecutable actual. Como la app no tiene consola ni ventana, arranca de forma invisible al iniciar sesión. Si mueves el `.exe`, desmarca y vuelve a marcar la casilla para actualizar la ruta.

## Widgets

- **Quick Actions**: abrir Bloc de notas, abrir Calculadora, mostrar/ocultar el escritorio.
- **Media (auto-detect)**: una fila por cada app con sesión de reproducción activa en Windows (Spotify, una pestaña de Chrome/Edge, VLC, etc.), cada una con su badge de color, nombre y botones Anterior / Reproducir-Pausa / Siguiente. La lista se actualiza al abrir el panel.
- **Brightness (monitors)**: una fila por monitor con su nombre real y un slider de brillo (clic o arrastre; se aplica al soltar). Usa DDC/CI (API de configuración de monitores de Windows), así que funciona con monitores externos que lo tengan activado; los que no lo soportan muestran "Brightness control not available". Los paneles integrados de portátiles no usan DDC/CI y por ahora no se controlan.
- **Lyrics**: muestra la letra sincronizada de lo que sea que esté sonando (no solo Spotify - sigue al mismo "reproductor activo" que ya usa Windows para el resto del sistema), centrada en el punto de la canción en que estabas cuando abriste el panel (no sigue la canción en vivo mientras el panel permanece abierto, para no necesitar un timer). No requiere cuenta ni configuración: las letras vienen de [LRCLIB](https://lrclib.net), una base de datos pública y gratuita hecha para esto, sin login ni API key.

El valor por defecto trae una pestaña de Quick Actions, Media y Brightness; Lyrics se agrega desde Settings → Add si la quieres usar.

Las letras que se obtienen se guardan en caché local (`%LOCALAPPDATA%\EdgeDeck\lyrics_cache\`), así que no se vuelve a pedir por red la próxima vez que suene la misma canción. Al ser una base comunitaria, alguna canción muy nueva o poco común puede no tener letra sincronizada disponible todavía.

## Notas técnicas

Cada pestaña es event-driven: sin render loop ni sondeo del mouse (usa `TrackMouseEvent`/`WM_MOUSELEAVE`), los temporizadores solo corren durante la animación de apertura/cierre o la breve espera antes de cerrar. No hay hooks globales de mouse ni de teclado (salvo el atajo de salida registrado con `RegisterHotKey`). Media, Brightness y Lyrics hacen su trabajo en un job corto del thread pool disparado únicamente por una acción del usuario (abrir el panel, pulsar un botón, mover un slider); no hay threads, timers ni polling permanentes. Un slider de brillo muestra el valor en vivo mientras lo arrastras, pero la escritura DDC/CI al monitor se hace una sola vez, al soltar (varios monitores guardan el brillo en memoria no volátil, así que no conviene escribir de forma continua). Lyrics revisa primero el caché local (costo cero de red) antes de llamar a LRCLIB. La app se limita a una instancia para evitar duplicar ventanas y el atajo global.
