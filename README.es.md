# EdgeDeck

[![build](https://github.com/BolsitaDP/edgedeck/actions/workflows/build.yml/badge.svg)](https://github.com/BolsitaDP/edgedeck/actions/workflows/build.yml)
[![Licencia: MIT](https://img.shields.io/badge/licencia-MIT-blue.svg)](LICENSE)

*[Read in English](README.md)*

EdgeDeck pone pestañas pequeñas en el borde de la pantalla. Deja el puntero sobre una y se desliza
un panel: lanzadores, controles de reproducción, volumen, brillo de los monitores, letras de
canciones, disposiciones de pantallas, tus últimas descargas. Aparta el puntero y vuelve a su
sitio.

Es una utilidad nativa de Windows escrita en C++ con Win32 y Direct2D —sin Electron, sin web view,
sin .NET— pensada para que te olvides de que está:

- **Nunca se lleva el foco.** Los paneles se abren sin quitarle el teclado a la app en la que
  escribes, y nada de lo que hace EdgeDeck te pone un cuadro de diálogo delante.
- **No cuesta casi nada.** Unos 20 MB de memoria y nada de CPU en reposo: sin sondeos, sin
  temporizadores, sin hooks globales de ratón. Todo ocurre como respuesta a un evento.
- **No estorba.** Sobre un juego o un vídeo a pantalla completa las pestañas se apartan, como la
  barra de tareas, y *Pausar pestañas* lo oculta todo hasta que lo quieras de vuelta.
- **No envía nada a ninguna parte.** Sin telemetría ni cuentas. La única petición de red es la de
  la pestaña Letras, que pregunta a [LRCLIB](https://lrclib.net) por la canción que suena, y solo
  si añades esa pestaña.

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/images/panels-dark-es.png">
  <img alt="Los paneles de EdgeDeck: Acciones rápidas, Multimedia, Volumen, Letras, Brillo, Archivos recientes y Pantallas" src="docs/images/panels-light-es.png" width="968">
</picture>

## Las pestañas

Cada pestaña muestra un tipo de panel. Se añaden, quitan, mueven y redimensionan en Configuración;
puedes tener varias del mismo tipo, en el borde izquierdo o derecho de cualquier monitor.

| Pestaña | Qué hace su panel |
|---|---|
| **Acciones rápidas** | Tu propia lista de cosas que abrir —programas, documentos, carpetas, direcciones web—, cada una con su icono. Se edita en su propia ventana, o arrastrando archivos y accesos directos desde el Explorador. |
| **Archivos recientes** | Los archivos más nuevos de una carpeta (Descargas por defecto), con hace cuánto llegó cada uno. Clic para abrirlo. |
| **Multimedia** | Cada app que está reproduciendo algo —Spotify, una pestaña del navegador, VLC— con anterior, reproducir/pausa y siguiente. |
| **Volumen** | Un deslizador y un botón de silencio por cada dispositivo de salida de audio. |
| **Brillo** | Un deslizador de brillo por cada monitor externo compatible con DDC/CI, y para la pantalla integrada de un portátil. |
| **Letras** | La letra de lo que suene, de LRCLIB: línea a línea al ritmo de la canción donde LRCLIB la tiene sincronizada, y si no, el texto sin más, que avanza con la canción. |
| **Pantallas** | Un clic para cambiar qué monitores están encendidos. Guarda tus propias disposiciones (por ejemplo "monitores del escritorio" o "solo la TV") en **Disposiciones...**; sin ninguna guardada ofrece todos los monitores y cada uno solo. |

## Instalación

Necesitas Windows 10 (versión 1809 o posterior) o Windows 11, de 64 bits.

Descarga **`EdgeDeck-<versión>-setup-x64.exe`** de la
[última release](https://github.com/BolsitaDP/edgedeck/releases/latest) (`setup-arm64` para un
equipo Windows on ARM) y ejecútalo. Instala solo para tu usuario, sin permisos de administrador, en
español o inglés, y pregunta si EdgeDeck debe iniciarse con Windows. Ejecutar uno más nuevo después
actualiza EdgeDeck en su sitio: lo cierra y lo vuelve a abrir.

¿Prefieres sin instalador? El `EdgeDeck-<versión>-win-x64.zip` tiene el mismo programa:
descomprímelo donde quieras y haz doble clic en **`Install.cmd`**.

EdgeDeck aún no está firmado digitalmente, así que SmartScreen puede avisar de un editor
desconocido (*Más información* > *Ejecutar de todas formas*). Cada release publica el SHA-256 de sus
archivos para comprobar que la descarga es la publicada.

EdgeDeck aparece en **Configuración de Windows > Aplicaciones > Aplicaciones instaladas**, desde
donde se desinstala como cualquier otra. La configuración, en `%LOCALAPPDATA%\EdgeDeck`, se
conserva (`uninstall.ps1 -RemoveSettings` del zip también la borra). *Buscar actualizaciones* en el menú de
EdgeDeck abre la última release; EdgeDeck nunca lo comprueba por su cuenta.

## Uso

- **Deja el puntero sobre una pestaña** para abrir su panel. Sigue abierto mientras el puntero esté
  sobre la pestaña o el panel, y se cierra un momento después de salir.
- **Fija un panel** con la chincheta de su esquina superior derecha para que quede abierto; vuelve a
  pulsarla para soltarlo.
- **Mueve una pestaña arriba o abajo** manteniendo pulsadas las flechas de la esquina superior
  izquierda de su panel y arrastrando; la pestaña se queda donde la sueltes.
- **Clic derecho en una pestaña, o en el icono de EdgeDeck del área de notificación,** para el
  menú: Configuración, Editar acciones rápidas, Pausar pestañas, Iniciar con Windows, Abrir
  registro, Reiniciar, Buscar actualizaciones, Informar de un problema y Salir. Clic izquierdo en el
  icono abre Configuración.
- **Configuración** es donde se añaden y colocan las pestañas, y donde están el tema, el idioma, el
  retardo de apertura y las demás opciones de abajo.

### Teclado

Los paneles nunca le quitan el foco a la app que estás usando, así que se manejan con el teclado
mientras el puntero está sobre ellos. Los atajos globales de abajo son los predeterminados: cámbialos,
o desactiva cualquiera, en **Configuración > Atajos...**; por ejemplo, si otro programa ya usa uno.

| Teclas | Qué hacen |
|---|---|
| `Ctrl+Alt+1` ... `Ctrl+Alt+9` | Abrir y fijar la pestaña 1 a 9 |
| `Ctrl+Alt+P` | Pausar o reanudar las pestañas |
| `Ctrl+Alt+Q` | Salir de EdgeDeck |
| `Ctrl+Alt+Mayús+1` ... `9` | Cambiar a la disposición 1 a 9 sin abrir el panel (solo con una pestaña Pantallas) |
| Flechas | Moverse entre los controles del panel abierto; izquierda y derecha también ajustan un deslizador |
| Rueda del ratón | Ajustar el control enfocado |
| `Intro` / `Espacio` | Activar el control enfocado |
| `Esc` | Desfijar y cerrar el panel |

Los lectores de pantalla reciben qué es cada panel y cada control.

## Configuración

Todo se ajusta desde la ventana de Configuración y se guarda en
`%LOCALAPPDATA%\EdgeDeck\config.txt`, un archivo de texto que también puedes editar a mano con
EdgeDeck cerrado.

| Ajuste | En Configuración | En `config.txt` |
|---|---|---|
| Idioma (al principio, el de Windows si es español; si no, inglés) | **Idioma**: English, Español | `language=es` |
| Atajos | **Atajos...** | `shortcutExit=Ctrl+Alt+Q`, `shortcutPause=`, `shortcutTabs=Ctrl+Alt`, `shortcutDisplays=Ctrl+Alt+Shift` (`none` desactiva uno) |
| Tema | **Tema**: igual que Windows, siempre oscuro, siempre claro | `theme=Follow` |
| Retardo de apertura | **Retardo**: cuánto debe quedarse el puntero antes de abrir | `openDelayMs=0` |
| Apps a pantalla completa | **Ocultar en pantalla completa** | `fullscreenGuard=1` |
| Iniciar con Windows | **Iniciar con Windows** | (la lista de inicio de Windows) |
| Carpeta de recientes | **Recientes**, o soltar una carpeta sobre la ventana | `recentFolder=` (vacío: Descargas) |
| Cada pestaña | tipo, monitor, borde, posición vertical, tamaño de pestaña y panel | una sección `[tab]` cada una |
| Acciones rápidas | **Acciones...** (con una pestaña Acciones rápidas seleccionada) | una sección `[action]` cada una |
| Reiniciar tras un fallo | - | `restartAfterCrash=1` |
| Disposiciones de pantallas | **Disposiciones...** (con una pestaña Pantallas seleccionada) | una sección `[layout]` cada una: `name=`, `monitors=` (ids EDID) |

Los paneles siguen el color de acento de Windows, el modo claro u oscuro y los colores de alto
contraste.

Si algo falla, EdgeDeck lo anota en `%LOCALAPPDATA%\EdgeDeck\edgedeck.log` (*Abrir registro* en el
menú). Tras un cierre inesperado se reinicia una vez y deja un pequeño volcado junto al registro,
que es lo más útil para adjuntar a un informe de error. *Informar de un problema...* en el menú abre
un informe de error nuevo con tus versiones de EdgeDeck y de Windows y tus monitores ya rellenados, y
deja el final del registro en el portapapeles para pegarlo (tu carpeta de usuario va oculta); nada se
envía hasta que lo publiques.

Si `config.txt` no se puede leer, o lo guardó un EdgeDeck más nuevo, EdgeDeck lo avisa al iniciar y
guarda una copia como `config.backup.txt` antes de escribir encima.

## Compilar desde el código

Necesitas CMake 3.20 o posterior y Visual Studio 2022 (o sus Build Tools) con la carga de trabajo
de C++ para escritorio y un Windows SDK.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

El runtime de C++ va enlazado dentro del ejecutable, así que `EdgeDeck.exe` funciona en un Windows
recién instalado sin ningún redistribuible. Para usar tu compilación como lo hace la release, usa
`scripts\install.ps1 -Build`; `scripts\package.ps1` genera el zip de la release (`-Architecture arm64`
para Windows on ARM, que necesita las herramientas de compilación ARM64 de MSVC).

## Contribuir

Los informes de errores, las traducciones y los pull requests son bienvenidos.
[CONTRIBUTING.md](CONTRIBUTING.md) (en inglés) explica cómo está organizado el código, las reglas
que respeta cada cambio (la principal: EdgeDeck debe seguir sin costar casi nada) y cómo probar. Las
notas de diseño de las decisiones menos obvias están en [docs/DESIGN.md](docs/DESIGN.md), y las
herramientas de prueba de escritorio en [tools/README.md](tools/README.md).

## Licencia

[MIT](LICENSE).
