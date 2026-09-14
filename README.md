# EdgeDeck

EdgeDeck es un panel nativo y pequeño para Windows, anclado al borde derecho del monitor principal. Incluye tres acciones: abrir Bloc de notas, abrir Calculadora y mostrar u ocultar el escritorio.

## Compilación

Requiere Windows 10 versión 1809 o posterior, CMake 3.20+, Visual Studio 2022 Build Tools con C++ para escritorio y un Windows SDK.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Ejecuta `build/Release/EdgeDeck.exe`.

## Uso

- Pasa el cursor sobre la pestaña para abrir el panel temporalmente.
- Haz clic en la pestaña para mantener el panel abierto; otro clic lo cierra. Con el panel activo, Arriba/Abajo selecciona una acción, Intro o Espacio la ejecuta y Escape lo cierra.
- `Ctrl+Shift+Alt+E` abre o cierra el panel desde el teclado.
- Haz clic derecho en la pestaña y elige **Exit**, o pulsa `Ctrl+Shift+Alt+Q`.
- Los botones de Spotify permiten ir a la canción anterior, reproducir o pausar, y pasar a la siguiente. Spotify debe estar abierto y exponer una sesión de reproducción de Windows; estos botones no controlan otras aplicaciones de audio.

El panel responde a cambios de pantalla y escala mientras está abierto. Usa mensajes de Windows, sin sondeo en segundo plano; los temporizadores solo funcionan durante la animación o la breve espera antes del cierre. Los controles de Spotify hacen su trabajo de forma asíncrona únicamente al pulsar un botón y no necesitan OAuth ni solicitudes de red periódicas. Se limita a una instancia para evitar duplicar ventanas y atajos globales.

La lista de acciones, el monitor principal y el borde derecho todavía se definen en el código.

Las letras no forman parte de esta primera etapa. Requieren una fuente independiente con permisos para mostrarlas; la referencia pública de la API de Spotify no documenta un endpoint de letras.
