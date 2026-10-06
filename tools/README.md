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
| `dispcfg` | Lista qué monitores están encendidos, valida o aplica una disposición (`main`, `tv`, `all`), guarda y restaura una instantánea. Usa el mismo `DisplayTopology` que la pestaña Displays, así que lo que dice vale para la app. |
| `settingsharness` | Abre **solo** la ventana de Settings (sin pestañas, sin atajos, sin candado de instancia) e imprime un informe de maquetación: controles fuera de la ventana, controles solapados, textos más anchos que su control. Con `dpi=N` simula el `WM_DPICHANGED` de un monitor a esa escala sin tocar tu configuración de pantalla. |
| `apptest` | La `App` **real** dentro de otro proceso, sin el candado de instancia única, así que convive con tu EdgeDeck. Lee ficheros `trig*.txt` de una carpeta y los entrega a la primera pestaña como lo haría `PanelWidget::ReportProblem`; `quit.txt` la cierra. |
| `backdrop` | Una ventana lisa siempre encima, para que una captura de lo que haya delante no contenga nada de tu escritorio. |
| `fullscreenwin` | Una ventana sin bordes que se pone en primer plano, para hacerse pasar por un juego a pantalla completa. No es *topmost* a propósito: las pestañas sí lo son y deben quedar por encima. |

## Los scripts

Todos devuelven un código de salida distinto de cero si algo falla. `-ToolsDir` en todos.

| Script | Qué comprueba | Qué hace a tu equipo |
|---|---|---|
| `Test-SettingsLayout.ps1` | La maquetación de Settings a 100–250 % (`-Dpis 96,144`, `-ScreenshotDir`). | Abre una ventana unos segundos. Seguro. |
| `Test-FullscreenGuard.ps1` | Que el hover no abre el panel sobre una app a pantalla completa y sí en los demás casos. | Una ventana oscura cubre el monitor principal ~10 s; mueve el cursor. |
| `Test-Notices.ps1` | El aviso de error en la cabecera del panel y por la bandeja (`-ScreenshotDir` para verlos). | Mueve el cursor; puede aparecer una notificación. |
| `Capture-TabIcons.ps1` | Una imagen con el icono de cada pestaña, ampliada (`-Path`, `-Zoom`). | Pone una tira lisa en el borde derecho unos segundos. |
| `Measure-Perf.ps1` | Memoria, hilos, handles y CPU de dos compilaciones, alternadas (`-ExeA`, `-ExeB`, `-Runs`). | Mueve el cursor ~20 s por pasada. **Exige cerrar el EdgeDeck real** (`-StopRunningInstance`). |
| `Test-DisplaySwitching.ps1` 🖥️ | Que `dispcfg` cambia de disposición y vuelve. Por defecto solo enciende y apaga la TV. | **Conmuta monitores.** Guarda una instantánea y termina dejando la disposición inicial. |
| `Test-DisplayHotkeys.ps1` 🖥️ | Los atajos `Ctrl+Alt+Shift+1/2/3` con el panel sin abrir. | **Conmuta monitores y pulsa teclas.** Exige cerrar el EdgeDeck real, que es quien las tiene. |

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
