# EdgeDeck

EdgeDeck es una utilidad nativa y ligera para Windows: varias pestañas pequeñas ancladas al borde derecho del monitor principal, cada una con su propio panel desplegable.

## Compilación

Requiere Windows 10 versión 1809 o posterior, CMake 3.20+, Visual Studio 2022 Build Tools con C++ para escritorio y un Windows SDK.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Ejecuta `build/Release/EdgeDeck.exe`.

## Uso

- Pasa el cursor sobre una pestaña para abrir su panel; al sacar el mouse se cierra solo tras una breve espera.
- Cada panel tiene un botón de pin (círculo, arriba a la derecha): fijado, el panel se queda abierto aunque muevas el mouse o hagas clic en una acción, hasta que lo desfijes o hagas clic fuera de él.
- Clic derecho en cualquier pestaña → **Settings...** abre la ventana de configuración: tipo de widget por pestaña, posición vertical, tamaño de la pestaña y del panel. Los cambios se aplican al instante y se guardan en `%LOCALAPPDATA%\EdgeDeck\config.txt`.
- Clic derecho → **Exit**, o `Ctrl+Shift+Alt+Q` desde cualquier lugar, cierra la aplicación.

## Widgets

- **Quick Actions**: abrir Bloc de notas, abrir Calculadora, mostrar/ocultar el escritorio.
- **Media (auto-detect)**: Anterior / Reproducir-Pausa / Siguiente para lo que sea que Windows considere la sesión de reproducción activa en ese momento (Spotify, una pestaña de Chrome/Edge, VLC, etc.) - no está atado a una app en particular, sigue automáticamente la que esté sonando. Si ninguna app expone controles multimedia al sistema, los botones avisan en vez de fallar en silencio.

El valor por defecto trae una pestaña de cada tipo; agregar, quitar o reordenar pestañas se hace desde Settings.

## Notas técnicas

Cada pestaña es event-driven: sin render loop ni sondeo del mouse (usa `TrackMouseEvent`/`WM_MOUSELEAVE`), los temporizadores solo corren durante la animación de apertura/cierre o la breve espera antes de cerrar. El cierre por "clic afuera" de un panel fijado usa un hook de mouse de bajo nivel que solo se instala mientras algo está realmente fijado, y nunca activa ni roba el foco de otra ventana. Los controles de media hacen su trabajo de forma asíncrona solo al pulsar un botón, sin solicitudes periódicas. La app se limita a una instancia para evitar duplicar ventanas y el atajo global.
