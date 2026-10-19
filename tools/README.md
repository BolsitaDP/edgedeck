# Herramientas de prueba

Lo que los tests unitarios (`tests/`) no pueden cubrir: ventanas, ratón, pantallas, DPI.
Son herramientas de desarrollador, no parte del producto. Cada una existe porque encontró un
fallo real: botones tapados por un texto, controles solapados a ciertas escalas, un panel que
no se cerraba tras cambiar de monitor, un aviso que se perdía con un destinatario nulo.

> **Leer antes de ejecutar nada.** Los scripts **mueven el cursor real, pulsan teclas reales,
> abren ventanas sobre tu monitor principal** y algunos **apagan y encienden monitores**.
> Las configuraciones que usan son aisladas (nunca tocan tu `config.txt` ni tu log), pero el
> escritorio es el tuyo. Los marcados con 🖥️ conmutan monitores y se niegan a correr sin
> `-AllowMonitorSwitching`.

## Compilar

Las herramientas no se compilan por defecto. En una carpeta aparte, para no mezclarlas con la
compilación normal:

```powershell
cmake -S . -B build-tools -G "Visual Studio 17 2022" -A x64 -DEDGEDECK_BUILD_TOOLS=ON -DEDGEDECK_BUILD_TESTS=OFF
cmake --build build-tools --config Release
```

Quedan en `build-tools\tools\Release\`. Los scripts las buscan ahí, luego en
`build\tools\Release\`, o donde diga la variable de entorno `EDGEDECK_TOOLS_DIR` (o el
parámetro `-ToolsDir`). El CI las **compila**, pero no las ejecuta: necesitan una sesión de
escritorio. Compilarlas es lo que impide que se pudran cuando cambia el código al que llaman.

Se ejecutan desde PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File tools\scripts\Test-SettingsLayout.ps1
```

## Los programas

| Programa | Qué es |
|---|---|
| `dispcfg` | Lista qué monitores están encendidos (con su id EDID y el dispositivo GDI `gdi=\\.\DISPLAY2` por el que se dibujan, que es lo que une un id con un rectángulo de pantalla), marca cuál es el principal de Windows y cuál el "main" que usaría el botón de monitor principal solo (`<== primary`, `<== main`), valida o aplica una disposición (`main`, `tv`, `all`, `only`), guarda y restaura una instantánea. Usa el mismo `DisplayTopology` que la pestaña Displays, así que lo que dice vale para la app. |
| `settingsharness` | Abre **solo** la ventana de Settings (sin pestañas, sin atajos, sin candado de instancia) e imprime un informe de maquetación: controles fuera de la ventana, controles solapados, textos más anchos que su control. Con `dpi=N` simula el `WM_DPICHANGED` de un monitor a esa escala sin tocar tu configuración de pantalla (el tamaño lógico lo lee de la propia ventana, no lo lleva escrito). Con `drop=N` abre la lista del N-ésimo desplegable (2 = monitores), para ver sus etiquetas. Con `window=actions dropfiles=X,Y,ruta\|ruta` arrastra esos archivos sobre el punto (X,Y) del editor de acciones —un arrastre OLE de verdad, con el mismo objeto de datos que da el Explorador— e imprime la lista, la línea de estado y lo que guardaría *Save*; con `ole` inicia OLE antes de crear los controles. |
| `apptest` | La `App` **real** dentro de otro proceso, sin el candado de instancia única, así que convive con tu EdgeDeck. Lee ficheros `trig*.txt` de una carpeta y los entrega a la primera pestaña como lo haría `PanelWidget::ReportProblem`; `quit.txt` la cierra. Instala el manejador de fallos como `WinMain` y se puede hacer caer a propósito: `crash.txt` provoca un acceso inválido real y `terminate.txt` una excepción sin capturar. Y los comandos del menú que no tienen ventana donde pulsar: `pause.txt` (pausar/reanudar), `restart.txt` (reiniciar con los mismos argumentos) y `editor.txt` (abrir el editor de acciones). |
| `backdrop` | Una ventana lisa siempre encima, para que una captura de lo que haya delante no contenga nada de tu escritorio. |
| `fullscreenwin` | Una ventana sin bordes que se pone en primer plano, para hacerse pasar por un juego a pantalla completa. No es *topmost* a propósito: las pestañas sí lo son y deben quedar por encima. |

## Los scripts

Todos devuelven un código de salida distinto de cero si algo falla. `-ToolsDir` en todos.

| Script | Qué comprueba | Qué hace a tu equipo |
|---|---|---|
| `Test-SettingsLayout.ps1` | La maquetación de Settings —o, con `-Dialog actions`, la del editor de acciones— a 100–250 % (`-Dpis 96,144`, `-ScreenshotDir`). Un texto largo que debe envolverse no cuenta como "demasiado ancho"; sí cuenta si, envuelto, no cabe en su caja. | Abre una ventana unos segundos. Seguro. |
| `Test-FullscreenGuard.ps1` | Que el hover no abre el panel sobre una app a pantalla completa y sí en los demás casos. | Una ventana oscura cubre el monitor principal ~10 s; mueve el cursor. |
| `Test-QuickActions.ps1` | El panel de acciones rápidas editable: alto según las filas (exactamente una fila más por fila añadida), que pulsar una fila arranca lo que nombra y solo eso, que una fila que no puede abrirse deja un aviso en la cabecera del panel (con un control: antes no hay ningún píxel de ese color) que un nombre con acento es una fila de verdad, y que cada fila muestra el icono de lo que abre: píxeles con color en su hueco para un programa y para una carpeta, y ninguno (una insignia gris con su letra) para un programa que no existe. | Mueve el cursor y pulsa en el panel de la copia de prueba; arranca (y luego cierra) ventanas lisas pequeñas. |
| `Test-ActionDrop.ps1` | Arrastrar archivos del Explorador al editor de acciones: que cae en cada tipo de control (la lista, los tres campos de texto —también con OLE iniciado antes de crearlos—, un texto, un botón, el borde vacío), que un archivo toma su nombre sin extensión y una carpeta el suyo entero, que lo arrastrado es lo que se guarda, que el arrastre termina como "enlace" (no se copia ni se mueve nada), que un nombre largo se recorta a 40 y que lo que no cabe (12 filas) se cuenta en la línea de estado. | Mueve el cursor y abre el editor unos segundos por caso. Crea y borra archivos en `%TEMP%`. |
| `Test-RecentFiles.ps1` | La pestaña Recent files: alto según los archivos (ocultos, descargas a medias y carpetas fuera), que un archivo nuevo aparece al volver a abrir el panel (exactamente una fila más), que un programa lleva icono y un documento insignia (cuánto del cuadro es un solo color), que pulsar un archivo lo abre (un acceso directo a la ventana lisa, visto por su línea de comandos), que un tipo sin programa deja un aviso y **no** abre la ventana "Abrir con", y los paneles cortos de una carpeta que no existe y de una vacía. | Mueve el cursor y pulsa en el panel de la copia de prueba; crea y borra archivos en `%TEMP%`; arranca (y cierra) una ventana lisa pequeña. |
| `Test-OpenDelayPause.ps1` | El retardo de apertura (un puntero que se va antes no abre nada; uno que se queda abre ni antes ni mucho después, con un control sin retardo), la pausa (oculta la pestaña y el puntero no abre nada; reanudar la devuelve) y "Restart" (la copia vieja sale, la nueva toma el relevo con los mismos argumentos, sin anunciarse como un fallo y sin quedar dos). | Mueve el cursor. Arranca y para sus propias copias de `apptest`. |
| `New-AppIcon.ps1` | **No es un test: genera `res\EdgeDeck.ico`** (9 tamaños) desde una descripción vectorial, y con `-Preview` una hoja para mirarlo sobre fondo oscuro y claro. Si cambias el dibujo, vuelve a ejecutarlo y compila. | Escribe el `.ico` del repositorio. Seguro. |
| `Test-PanelPaint.ps1` | Que el panel **está dibujado** —no solo "visible" para Windows— nada más posarse el cursor en la pestaña, durante el deslizamiento y al abrirlo por segunda vez, sin que el cursor tenga que entrar en él. Mira píxeles: una ventana lisa donde se abrirá el panel, y más de un color en la imagen es el panel. | Mueve el cursor; pone una ventana lisa ~15 s en cada borde del monitor principal. |
| `Test-TabPlacement.ps1` | Que cada pestaña se acopla al borde y al monitor configurados (derecho e izquierdo, el principal y otro, y uno desconectado que cae al principal), que su panel se abre junto a ella y dentro de su monitor, y —con dos monitores contiguos— que el panel al deslizarse **no se ve en el vecino**: pone una ventana lisa en esa franja, la captura ~460 veces mientras el panel entra y sale y cuenta los píxeles que no son suyos. Lleva un control (una ventana naranja que el escaneo debe ver) para que "0 píxeles ajenos" signifique algo. | Mueve el cursor; pone ventanas lisas ~10 s en el monitor vecino. No conmuta monitores. Con un solo monitor omite lo de dos. |
| `Test-Notices.ps1` | El aviso de error en la cabecera del panel y por la bandeja (`-ScreenshotDir` para verlos). | Mueve el cursor; puede aparecer una notificación. |
| `Capture-TabIcons.ps1` | Una imagen con el icono de cada pestaña, ampliada (`-Path`, `-Zoom`). | Pone una tira lisa en el borde derecho unos segundos. |
| `Measure-Perf.ps1` | Memoria, hilos, handles y CPU de dos compilaciones, alternadas (`-ExeA`, `-ExeB`, `-Runs`). | Mueve el cursor ~20 s por pasada. **Exige cerrar el EdgeDeck real** (`-StopRunningInstance`). |
| `Test-DisplaySwitching.ps1` 🖥️ | Que `dispcfg` cambia de disposición y vuelve. Por defecto solo enciende y apaga la TV. | **Conmuta monitores.** Guarda una instantánea y termina dejando la disposición inicial. |
| `Test-CrashHandling.ps1` | Provoca fallos reales en una copia de prueba y comprueba qué deja: línea en el log, minidump válido y **un solo** reinicio (y ninguno si el reiniciado cae enseguida o si `restartAfterCrash=0`). | Abre y mata procesos `apptest.exe` suyos. Seguro. |
| `Test-DisplayHotkeys.ps1` 🖥️ | Los atajos `Ctrl+Alt+Shift+1`..`4` con el panel sin abrir (el 4, el monitor principal solo, apaga también el C27 y vuelve). | **Conmuta monitores y pulsa teclas.** Exige cerrar el EdgeDeck real, que es quien las tiene. |

`UiHelpers.ps1` es la biblioteca común (buscar ventanas **por proceso**, ratón, teclado,
capturas, y `Start-TestApp` para arrancar una copia de prueba con configuración propia).

Para volver a abrir tu EdgeDeck tras un script que lo cerró: `scripts\install.ps1`.

## Cómo no equivocarse

- **Pestañas de tu EdgeDeck en la misma pantalla.** Las de la copia de prueba y las tuyas
  conviven en el borde derecho. Los scripts eligen las suyas **por proceso** (`Get-Wins -ProcessId`),
  nunca "la primera visible": esa elección se equivocaba cuando el orden de ventanas cambiaba.
- **El fondo liso va primero.** Con ventanas siempre encima manda la más reciente: un `backdrop`
  creado después de la app tapa la pestaña y se traga el hover.
- **Capturas.** `Take-Shot` copia lo que haya sobre ese rectángulo de pantalla. Apúntala solo a
  una ventana que esté sobre un `backdrop`, o a una ventana recortada a sus límites visibles
  (`Get-VisibleRect`, que excluye su borde invisible). Una captura con contenido ajeno se borra.
- **"Visible" no es "dibujado".** `IsWindowVisible` es cierto para una ventana con alfa por píxel a la que
  nadie ha dado imagen todavía. Los scripts que comprobaban ventanas (¿existe el panel? ¿dónde está?)
  pasaron durante semanas mientras el panel salía **vacío** al abrirse con el cursor sobre la pestaña:
  la primera imagen se presenta con la ventana fuera del escritorio, DWM no guarda la imagen de una
  ventana así, y el panel no se veía hasta que el cursor entraba en él y provocaba otro repintado
  (por eso casi nadie lo notaba: es lo que se hace justo después). Solo un script que mira píxeles
  lo ve: `Test-PanelPaint`. En un borde con otro monitor al otro lado no ocurre, porque el panel
  empieza dentro del escritorio, así que el fallo depende de cuál sea el borde.
- **`[math]::Min(1, 0.63)` devuelve 1.** Con un entero y un decimal, PowerShell elige la sobrecarga de
  enteros y redondea el decimal. `Test-TabPlacement` puso así una pestaña abajo del todo en vez de a
  media altura, y su comprobación de píxeles pasaba sin ver nada: el panel nunca cruzaba la franja
  vigilada. Se detectó porque se desactivó a propósito el recorte para ver que el test fallaba (y no
  fallaba). Un test que no se ha visto fallar no ha demostrado nada; usa literales `1.0` y `0.0`.
- **`powershell -File` y las listas.** Pasa una lista separada por comas como un solo texto, así que
  un parámetro `[int[]]` leería `96,144` como 96144. Los scripts que reciben listas las reciben
  como texto y las separan ellos.
- **Un caso que parecía fallo y era del entorno.** El "maximizado" de `Test-FullscreenGuard`
  fallaba en un equipo sin barra de tareas en el monitor principal: allí el área de trabajo *es* el
  monitor y una ventana sin bordes de ese tamaño es, con razón, una pantalla completa. El caso usa
  ahora el área de trabajo menos una barra de título, que es lo que tiene un maximizado de verdad.

## Añadir un escenario

1. Un script `Test-<Algo>.ps1` con ayuda en el encabezado (`.SYNOPSIS`, `.DESCRIPTION`, y una
   advertencia visible si hace algo más que abrir una ventana).
2. `. "$PSScriptRoot\UiHelpers.ps1"`, y `Start-TestApp` / `Stop-TestApp` (en `try`/`finally`) para
   la copia de prueba; `Send-TestApp` para inyectar un aviso.
3. Si necesita algo del código que ninguna herramienta expone, mejor ampliar `apptest` o
   `settingsharness` que construir otra: comparten las fuentes del producto.
4. Un script que conmute monitores o cierre el EdgeDeck real debe **negarse** sin un parámetro
   explícito, y dejar el escritorio como lo encontró aunque falle (`finally`).
