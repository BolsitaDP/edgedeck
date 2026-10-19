#pragma once

#include <string>

// Every text EdgeDeck shows a person, in each language it speaks. One table, so a string is added in
// one place with all its translations next to it, and a missing translation is a compile error
// rather than a blank in the interface. English is the default; the language is a setting
// ([settings] language=en|es), chosen in the Settings window.
//
// Texts with values in them are printf formats (%ls, %zu, %d, ...). Every translation must carry the
// same specifiers in the same order as the English one - a test checks it - because the arguments
// are passed once, whatever the language.
//
// The log is not translated: it is for whoever reads a bug report, and one language there is easier
// to search.

enum class Language : int {
    English = 0,
    Spanish = 1,
};

// X(id, English, Spanish)
#define EDGEDECK_STRINGS(X)                                                                                  \
    /* ---- shared ---- */                                                                                   \
    X(Checking, L"Checking...", L"Comprobando...")                                                           \
    X(ButtonAdd, L"Add", L"Añadir")                                                                          \
    X(ButtonRemove, L"Remove", L"Quitar")                                                                    \
    X(ButtonUp, L"Up", L"Subir")                                                                             \
    X(ButtonDown, L"Down", L"Bajar")                                                                         \
    X(ButtonSave, L"Save", L"Guardar")                                                                       \
    X(ButtonClose, L"Close", L"Cerrar")                                                                      \
    X(StartWithWindows, L"Start with Windows", L"Iniciar con Windows")                                       \
    X(AutostartOnFailed, L"Could not turn on Start with Windows: Windows would not accept the startup entry.", \
      L"No se pudo activar el inicio con Windows: Windows no aceptó la entrada de inicio.")                 \
    X(AutostartOffFailed,                                                                                    \
      L"Could not turn off Start with Windows: Windows would not let the startup entry be removed.",         \
      L"No se pudo desactivar el inicio con Windows: Windows no dejó quitar la entrada de inicio.")         \
    /* ---- opening things ---- */                                                                           \
    X(ShellNotFound, L"not found", L"no se encontró")                                                        \
    X(ShellAccessDenied, L"access denied", L"acceso denegado")                                               \
    X(ShellCancelled, L"cancelled", L"cancelado")                                                            \
    X(ShellNoAssociation, L"nothing is set to open that kind of file",                                       \
      L"ningún programa está configurado para abrir ese tipo de archivo")                                    \
    X(ShellBadFormat, L"not a valid program", L"no es un programa válido")                                   \
    X(ShellWindowsError, L"Windows error %lu", L"error de Windows %lu")                                      \
    X(CouldNotOpen, L"Could not open \"%ls\": %ls.", L"No se pudo abrir \"%ls\": %ls.")                      \
    X(ShowDesktopFailed, L"Windows could not show the desktop.", L"Windows no pudo mostrar el escritorio.") \
    /* ---- the quick-actions editor ---- */                                                                 \
    X(ActionsTitle, L"EdgeDeck Quick Actions", L"Acciones rápidas de EdgeDeck")                              \
    X(ActionsList, L"Actions", L"Acciones")                                                                  \
    X(ActionsName, L"Name:", L"Nombre:")                                                                     \
    X(ActionsOpens, L"Opens:", L"Abre:")                                                                     \
    X(ActionsBrowse, L"Browse...", L"Examinar...")                                                           \
    X(ActionsArguments, L"Arguments (optional):", L"Argumentos (opcional):")                                 \
    X(ActionsHelp,                                                                                           \
      L"It can open a program, a document, a folder or a web address (https://...). Variables such as "     \
      L"%ProgramFiles% are filled in. You can also drag files and folders here from Explorer.",              \
      L"Abre un programa, un documento, una carpeta o una web (https://...). Se completan variables como "   \
      L"%ProgramFiles%. También puedes arrastrar aquí archivos y carpetas del Explorador.")                  \
    X(NewAction, L"New action", L"Nueva acción")                                                             \
    X(NoName, L"(no name)", L"(sin nombre)")                                                                 \
    X(BrowseFilter, L"Programs and documents", L"Programas y documentos")                                    \
    X(BrowseTitle, L"Choose what this action opens", L"Elige qué abre esta acción")                          \
    X(DropListFull, L"The list is full (%zu actions). Remove one to make room.",                             \
      L"La lista está llena (%zu acciones). Quita una para hacer sitio.")                                    \
    X(DropLeftOutOne, L"Added %zu. The list holds %zu at most, so 1 was left out.",                          \
      L"Se añadieron %zu. La lista admite %zu como máximo, así que 1 quedó fuera.")                          \
    X(DropLeftOutMany, L"Added %zu. The list holds %zu at most, so %zu were left out.",                      \
      L"Se añadieron %zu. La lista admite %zu como máximo, así que %zu quedaron fuera.")                     \
    X(DropAddedOne, L"Added \"%ls\". Press Save to keep it.", L"Se añadió \"%ls\". Pulsa Guardar para conservarla.") \
    X(DropAddedMany, L"Added %zu actions. Press Save to keep them.",                                          \
      L"Se añadieron %zu acciones. Pulsa Guardar para conservarlas.")                                        \
    X(NeedsName, L"Give this action a name, or remove it.", L"Ponle un nombre a esta acción o quítala.")    \
    X(NeedsTarget, L"Say what this action opens (use Browse...), or remove it.",                             \
      L"Indica qué abre esta acción (usa Examinar...) o quítala.")                                           \
    X(DefaultNotepad, L"Open Notepad", L"Abrir Bloc de notas")                                               \
    X(DefaultCalculator, L"Open Calculator", L"Abrir Calculadora")                                           \
    X(DefaultDesktop, L"Show Desktop", L"Mostrar escritorio")                                                \
    /* ---- the app: notifications, the menu ---- */                                                         \
    X(Welcome,                                                                                               \
      L"EdgeDeck is running. Rest the pointer on the small tabs at the right edge of your screen to open "   \
      L"them. Right-click a tab, or this icon, for Settings.",                                               \
      L"EdgeDeck está en marcha. Deja el puntero sobre las pestañas pequeñas del borde derecho de la "       \
      L"pantalla para abrirlas. Clic derecho en una pestaña, o en este icono, para la configuración.")       \
    X(ConfigUnreadable, L"EdgeDeck could not read its settings file and started with the defaults.",         \
      L"EdgeDeck no pudo leer su archivo de configuración y empezó con los valores predeterminados.")         \
    X(ConfigNewer, L"A newer EdgeDeck saved these settings; what this version does not know will be lost.",  \
      L"Un EdgeDeck más nuevo guardó esta configuración; lo que esta versión no conoce se perderá.")         \
    X(ConfigBackupKept, L"A copy was kept as %ls.", L"Se guardó una copia como %ls.")                        \
    X(CrashRestarted, L"EdgeDeck closed unexpectedly and was restarted. Details are in its log.",            \
      L"EdgeDeck se cerró de forma inesperada y se reinició. Los detalles están en su registro.")            \
    X(ExitHotkeyTaken,                                                                                       \
      L"The exit shortcut (%ls) is already taken by another program. You can still exit from the EdgeDeck "  \
      L"icon in the notification area, or choose other keys in Settings > Shortcuts.",                       \
      L"Otro programa ya usa el atajo para salir (%ls). Puedes salir desde el icono de EdgeDeck en el área " \
      L"de notificación, o elegir otras teclas en Configuración > Atajos.")                                  \
    X(SaveFailedSettings,                                                                                    \
      L"EdgeDeck could not write its settings file. Your changes will apply for this session but will not "  \
      L"be kept.",                                                                                           \
      L"EdgeDeck no pudo escribir su archivo de configuración. Los cambios se aplican en esta sesión, pero " \
      L"no se conservarán.")                                                                                 \
    X(SaveFailedActions,                                                                                     \
      L"EdgeDeck could not write its settings file. Your actions will apply for this session but will not "  \
      L"be kept.",                                                                                           \
      L"EdgeDeck no pudo escribir su archivo de configuración. Las acciones se aplican en esta sesión, "     \
      L"pero no se conservarán.")                                                                            \
    X(RebuildFailed, L"EdgeDeck could not rebuild its panels. The previous layout is still in place.",       \
      L"EdgeDeck no pudo reconstruir sus paneles. Se mantiene la disposición anterior.")                     \
    X(TrayPaused, L"%ls (paused)", L"%ls (en pausa)")                                                        \
    X(AutostartNowOff, L"EdgeDeck will no longer start with Windows.", L"EdgeDeck ya no se iniciará con Windows.") \
    X(AutostartNowOn, L"EdgeDeck will start with Windows.", L"EdgeDeck se iniciará con Windows.")            \
    X(OpenLogFailed, L"EdgeDeck could not open its log file.", L"EdgeDeck no pudo abrir su archivo de registro.") \
    X(RestartNoPath, L"EdgeDeck could not work out where it is running from to restart.",                    \
      L"EdgeDeck no pudo averiguar desde dónde se ejecuta para reiniciarse.")                                \
    X(RestartNoCommand, L"EdgeDeck could not build the command to restart itself. It is still running.",     \
      L"EdgeDeck no pudo preparar la orden para reiniciarse. Sigue en marcha.")                              \
    X(RestartNoStart, L"EdgeDeck could not start a new copy of itself. It is still running.",                \
      L"EdgeDeck no pudo iniciar una copia nueva de sí mismo. Sigue en marcha.")                             \
    X(MenuSettings, L"Settings...", L"Configuración...")                                                     \
    X(MenuActions, L"Edit quick actions...", L"Editar acciones rápidas...")                                  \
    X(MenuPause, L"Pause tabs", L"Pausar pestañas")                                                          \
    X(MenuResume, L"Resume tabs", L"Reanudar pestañas")                                                      \
    X(MenuOpenLog, L"Open log", L"Abrir registro")                                                           \
    X(MenuRestart, L"Restart EdgeDeck", L"Reiniciar EdgeDeck")                                               \
    X(MenuUpdates, L"Check for updates...", L"Buscar actualizaciones...")                                    \
    X(MenuReport, L"Report a problem...", L"Informar de un problema...")                                     \
    X(ReportLogCopied, L"The end of EdgeDeck's log is on the clipboard: paste it into the report's Log box.", \
      L"El final del registro de EdgeDeck está en el portapapeles: pégalo en el campo Log del informe.")      \
    X(ReportNoClipboard, L"EdgeDeck could not put its log on the clipboard. Menu > Open log shows it.",       \
      L"EdgeDeck no pudo copiar su registro al portapapeles. Menú > Abrir registro lo muestra.")             \
    X(ReportOpenFailed, L"EdgeDeck could not open the report page in the browser.",                          \
      L"EdgeDeck no pudo abrir la página del informe en el navegador.")                                      \
    X(UpdatesOpenFailed, L"EdgeDeck could not open its releases page in the browser.",                       \
      L"EdgeDeck no pudo abrir su página de versiones en el navegador.")                                     \
    X(MenuExit, L"Exit", L"Salir")                                                                           \
    X(AutostartRepaired,                                                                                     \
      L"EdgeDeck was set to start with Windows, but pointed at a program that no longer exists, so it "      \
      L"would not have started. That is fixed now.",                                                         \
      L"EdgeDeck estaba configurado para iniciarse con Windows, pero apuntaba a un programa que ya no "      \
      L"existe, así que no se habría iniciado. Ya está corregido.")                                          \
    X(AutostartBroken,                                                                                       \
      L"EdgeDeck is set to start with Windows, but pointed at a program that no longer exists and could "    \
      L"not be fixed automatically. Turn \"Start with Windows\" off and on again in Settings.",              \
      L"EdgeDeck está configurado para iniciarse con Windows, pero apunta a un programa que ya no existe y " \
      L"no se pudo corregir solo. Desactiva y vuelve a activar \"Iniciar con Windows\" en Configuración.")   \
    X(AutostartMissing,                                                                                      \
      L"EdgeDeck is not set to start with Windows. Right-click its icon in the notification area and "       \
      L"choose \"Start with Windows\".",                                                                     \
      L"EdgeDeck no está configurado para iniciarse con Windows. Haz clic derecho en su icono del área de "  \
      L"notificación y elige \"Iniciar con Windows\".")                                                      \
    /* ---- Quick Actions ---- */                                                                            \
    X(QuickActionsTitle, L"Quick Actions", L"Acciones rápidas")                                              \
    X(QuickActionsSummary, L"Quick Actions.", L"Acciones rápidas.")                                          \
    /* ---- Recent files ---- */                                                                             \
    X(RecentTitle, L"Recent files", L"Archivos recientes")                                                   \
    X(RecentFolderNotFound, L"Folder not found", L"No se encontró la carpeta")                               \
    X(RecentNoFiles, L"No files in this folder", L"No hay archivos en esta carpeta")                         \
    X(RecentOpenFolder, L"Open folder", L"Abrir carpeta")                                                    \
    X(RecentTheFolder, L"the folder", L"la carpeta")                                                         \
    X(RecentAWhileAgo, L"a while ago", L"hace tiempo")                                                       \
    X(RecentSummaryLoading, L"Recent files.", L"Archivos recientes.")                                        \
    X(RecentSummaryNotFound, L"Recent files. The folder was not found.",                                     \
      L"Archivos recientes. No se encontró la carpeta.")                                                     \
    X(RecentSummary, L"Recent files, %zu shown.", L"Archivos recientes, se muestran %zu.")                   \
    X(RecentItem, L"%ls, %ls", L"%ls, %ls")                                                                  \
    X(AgeJustNow, L"just now", L"ahora mismo")                                                               \
    X(AgeMinutes, L"%lld min ago", L"hace %lld min")                                                         \
    X(AgeHours, L"%lld h ago", L"hace %lld h")                                                               \
    X(AgeYesterday, L"yesterday", L"ayer")                                                                   \
    X(AgeDays, L"%lld days ago", L"hace %lld días")                                                          \
    /* ---- Media ---- */                                                                                    \
    X(MediaTitle, L"Media", L"Multimedia")                                                                   \
    X(MediaNothing, L"No media playing", L"No se reproduce nada")                                            \
    X(MediaNoTrack, L"No track information", L"Sin información de la pista")                                 \
    X(MediaControlFailed, L"Windows could not control this app.", L"Windows no pudo controlar esta app.")   \
    X(MediaUnsupported, L"This app does not support that control right now.",                                \
      L"Esta app no admite ese control ahora.")                                                              \
    X(MediaSummaryNothing, L"Media. No media playing.", L"Multimedia. No se reproduce nada.")                \
    X(MediaSummaryOne, L"Media. %zu app playing.", L"Multimedia. %zu app reproduciendo.")                    \
    X(MediaSummaryMany, L"Media. %zu apps playing.", L"Multimedia. %zu apps reproduciendo.")                 \
    X(MediaPrevious, L"%ls: previous track. %ls", L"%ls: pista anterior. %ls")                               \
    X(MediaPlay, L"%ls: play. %ls", L"%ls: reproducir. %ls")                                                 \
    X(MediaPause, L"%ls: pause. %ls", L"%ls: pausar. %ls")                                                   \
    X(MediaNext, L"%ls: next track. %ls", L"%ls: pista siguiente. %ls")                                      \
    /* ---- Volume ---- */                                                                                   \
    X(VolumeTitle, L"Volume", L"Volumen")                                                                    \
    X(VolumeNoDevices, L"No audio output devices", L"No hay dispositivos de salida de audio")                \
    X(VolumeChecking, L"Checking audio...", L"Buscando audio...")                                            \
    X(VolumeDefault, L"%ls  (default)", L"%ls  (predeterminado)")                                            \
    X(VolumeSpeakers, L"Speakers", L"Altavoces")                                                             \
    X(VolumeSummaryNone, L"Volume. No audio output devices.", L"Volumen. No hay dispositivos de salida de audio.") \
    X(VolumeSummaryOne, L"Volume. %zu output device.", L"Volumen. %zu dispositivo de salida.")               \
    X(VolumeSummaryMany, L"Volume. %zu output devices.", L"Volumen. %zu dispositivos de salida.")            \
    X(VolumeMuted, L"%ls: muted. Activate to unmute.", L"%ls: silenciado. Actívalo para quitar el silencio.") \
    X(VolumeUnmuted, L"%ls: unmuted. Activate to mute.", L"%ls: con sonido. Actívalo para silenciar.")       \
    X(VolumeLevel, L"%ls: volume %d percent.", L"%ls: volumen al %d por ciento.")                            \
    /* ---- Brightness ---- */                                                                               \
    X(BrightnessTitle, L"Brightness", L"Brillo")                                                             \
    X(BrightnessChecking, L"Checking displays...", L"Buscando pantallas...")                                 \
    X(NoMonitors, L"No monitors found", L"No se encontraron monitores")                                      \
    X(BrightnessUnavailable, L"Brightness control not available", L"Control de brillo no disponible")        \
    X(GenericMonitor, L"Generic monitor", L"Monitor genérico")                                               \
    X(BrightnessSummaryNone, L"Brightness. No monitors found.", L"Brillo. No se encontraron monitores.")     \
    X(BrightnessSummaryOne, L"Brightness. %zu monitor.", L"Brillo. %zu monitor.")                            \
    X(BrightnessSummaryMany, L"Brightness. %zu monitors.", L"Brillo. %zu monitores.")                        \
    X(BrightnessRowUnavailable, L"%ls: brightness control not available.", L"%ls: control de brillo no disponible.") \
    X(BrightnessLevel, L"%ls: brightness %d percent.", L"%ls: brillo al %d por ciento.")                     \
    /* ---- Lyrics ---- */                                                                                   \
    X(LyricsTitle, L"Lyrics", L"Letras")                                                                     \
    X(LyricsLooking, L"Looking up lyrics...", L"Buscando la letra...")                                       \
    X(LyricsNothingPlaying, L"Nothing playing right now", L"No se está reproduciendo nada")                  \
    X(LyricsNone, L"No synced lyrics for this track", L"No hay letra sincronizada para esta pista")          \
    X(LyricsUnreachable, L"Could not reach the lyrics service", L"No se pudo conectar con el servicio de letras") \
    X(LyricsSummaryNothing, L"Lyrics. Nothing to show.", L"Letras. Nada que mostrar.")                       \
    X(LyricsSummaryTrack, L"Lyrics for %ls.", L"Letra de %ls.")                                              \
    X(LyricsSummaryLine, L"Lyrics for %ls. %ls", L"Letra de %ls. %ls")                                       \
    /* ---- Displays ---- */                                                                                 \
    X(DisplaysTitle, L"Displays", L"Pantallas")                                                              \
    X(ProfileMainMonitors, L"Monitors 1 + 2", L"Monitores 1 + 2")                                            \
    X(ProfileTvOnly, L"TV only", L"Solo la TV")                                                              \
    X(ProfileAll, L"All monitors", L"Todos los monitores")                                                   \
    X(ProfileMainOnly, L"Main monitor only", L"Solo el monitor principal")                                   \
    X(LayoutOnly, L"Only %ls", L"Solo %ls")                                                                  \
    X(DisplaysNothingToSwitch, L"Connect another monitor to switch between them.",                           \
      L"Conecta otro monitor para poder alternar entre ellos.")                                              \
    X(DisplaysMissing, L"Not connected: %ls", L"No conectado: %ls")                                          \
    X(DisplaysCount, L"%zu monitors", L"%zu monitores")                                                      \
    X(DisplaysSwitching, L"Switching...", L"Cambiando...")                                                   \
    X(DisplaysActive, L"Active", L"Activo")                                                                  \
    X(DisplaysOneMissing, L"One of those monitors is not connected right now.",                              \
      L"Uno de esos monitores no está conectado ahora.")                                                     \
    X(DisplaysApplyFailed, L"Windows could not change the display layout (error %ld).",                      \
      L"Windows no pudo cambiar la disposición de pantallas (error %ld).")                                   \
    X(DisplaysSummary, L"Displays.", L"Pantallas.")                                                          \
    X(DisplaysSummaryActive, L"%ls is active.", L"%ls está activo.")                                         \
    X(DisplaysRow, L"%ls: %ls", L"%ls: %ls")                                                                 \
    X(DisplaysRowActive, L"%ls: %ls. Active", L"%ls: %ls. Activo")                                           \
    /* ---- Settings ---- */                                                                                 \
    X(SettingsTitle, L"EdgeDeck Settings", L"Configuración de EdgeDeck")                                     \
    X(SettingsTabs, L"Tabs", L"Pestañas")                                                                    \
    X(SettingsWidgetType, L"Widget type:", L"Tipo de widget:")                                               \
    X(SettingsActions, L"Actions...", L"Acciones...")                                                        \
    X(SettingsPosition, L"Vertical position:", L"Posición vertical:")                                        \
    X(SettingsTabWidth, L"Tab width", L"Ancho pest.")                                                      \
    X(SettingsTabHeight, L"Tab height", L"Alto pest.")                                                     \
    X(SettingsPanelWidth, L"Panel width", L"Ancho panel")                                                    \
    X(SettingsMonitor, L"Monitor:", L"Monitor:")                                                             \
    X(SettingsEdge, L"Edge:", L"Borde:")                                                                     \
    X(SettingsRecentFolder, L"Recent folder:", L"Recientes:")                                                \
    X(SettingsRecentDefault, L"Downloads (default)", L"Descargas (predeterminada)")                          \
    X(SettingsTheme, L"Theme:", L"Tema:")                                                                    \
    X(SettingsLanguage, L"Language:", L"Idioma:")                                                            \
    X(SettingsDelay, L"Open delay:", L"Retardo:")                                                            \
    X(SettingsFullscreen, L"Don't open over full-screen apps", L"No abrir sobre pantalla completa")   \
    X(SettingsKeyboard, L"Keyboard", L"Teclado")                                                             \
    X(SettingsKeyboardHelp,                                                                                  \
      L"%ls: open and pin a panel.   %ls: exit EdgeDeck.\r\n"                                                \
      L"%ls: pause or resume the tabs.\r\n"                                                                  \
      L"%ls: switch monitors (needs a Displays tab).\r\n"                                                    \
      L"In an open panel, arrow keys or the wheel move and adjust,\r\n"                                      \
      L"Enter or Space activate, and Escape unpins and closes it.",                                          \
      L"%ls: abrir y fijar un panel.   %ls: salir.\r\n"                                                      \
      L"%ls: pausar o reanudar las pestañas.\r\n"                                                            \
      L"%ls: cambiar de monitores (pestaña Pantallas).\r\n"                                                  \
      L"En un panel abierto, las flechas o la rueda mueven y ajustan,\r\n"                                   \
      L"Intro o Espacio activan, y Escape lo desfija y lo cierra.")                                          \
    X(SettingsShortcuts, L"Shortcuts...", L"Atajos...")                                                      \
    X(SettingsLayouts, L"Layouts...", L"Disposiciones...")                                                   \
    /* ---- the display layouts window ---- */                                                               \
    X(LayoutsTitle, L"EdgeDeck Display Layouts", L"Disposiciones de pantallas de EdgeDeck")                  \
    X(LayoutsList, L"Layouts", L"Disposiciones")                                                             \
    X(LayoutsAddCurrent, L"Add current", L"Añadir la actual")                                                \
    X(LayoutsMonitors, L"Monitors it turns on:", L"Monitores que enciende:")                                 \
    X(LayoutsHelp,                                                                                           \
      L"A layout turns its monitors on and the others off. Its place in the list is its shortcut "           \
      L"(Settings > Shortcuts). With no layouts, the panel offers all monitors and each one alone.",         \
      L"Una disposición enciende sus monitores y apaga los demás. Su lugar en la lista es su atajo "         \
      L"(Configuración > Atajos). Sin disposiciones, el panel ofrece todos los monitores y cada uno solo.")  \
    X(LayoutsNeedName, L"Give this layout a name, or remove it.", L"Ponle un nombre a esta disposición o quítala.") \
    X(LayoutsNeedMonitor, L"Tick at least one monitor for this layout, or remove it.",                       \
      L"Marca al menos un monitor para esta disposición o quítala.")                                         \
    X(LayoutsFull, L"There are already %zu layouts, the most there can be.",                                 \
      L"Ya hay %zu disposiciones, el máximo.")                                                               \
    /* ---- the shortcuts window ---- */                                                                     \
    X(ShortcutsTitle, L"EdgeDeck Shortcuts", L"Atajos de EdgeDeck")                                          \
    X(ShortcutExitLabel, L"Exit EdgeDeck:", L"Salir de EdgeDeck:")                                           \
    X(ShortcutPauseLabel, L"Pause or resume:", L"Pausar o reanudar:")                                        \
    X(ShortcutTabsLabel, L"Open tab 1 to 9:", L"Abrir pestaña 1 a 9:")                                       \
    X(ShortcutDisplaysLabel, L"Display layout 1 to 9:", L"Disposición 1 a 9:")                        \
    X(ShortcutsHelp,                                                                                         \
      L"Click a box and press the keys you want. Backspace leaves it without a shortcut. Each one needs "    \
      L"Ctrl, Alt or Win.",                                                                                  \
      L"Haz clic en un cuadro y pulsa las teclas que quieras. Retroceso lo deja sin atajo. Cada uno "        \
      L"necesita Ctrl, Alt o Win.")                                                                          \
    X(ShortcutOff, L"Off", L"Desactivado")                                                                   \
    X(ShortcutNone, L"no shortcut", L"sin atajo")                                                            \
    X(ShortcutNameExit, L"Exit", L"Salir")                                                                   \
    X(ShortcutNamePause, L"Pause", L"Pausar")                                                                \
    X(ShortcutNameTabs, L"Open tab", L"Abrir pestaña")                                                       \
    X(ShortcutNameDisplays, L"Switch displays", L"Cambiar pantallas")                                        \
    X(ShortcutNeedsModifier, L"\"%ls\" needs Ctrl, Alt or Win, or it would take the key from everything you type.", \
      L"\"%ls\" necesita Ctrl, Alt o Win; si no, le quitaría la tecla a todo lo que escribes.")              \
    X(ShortcutClash, L"\"%ls\" and \"%ls\" would be the same keys.", L"\"%ls\" y \"%ls\" serían las mismas teclas.") \
    X(ShortcutTaken, L"Saved, but another program already uses %ls. Choose other keys for it.",              \
      L"Guardado, pero otro programa ya usa %ls. Elige otras teclas para ese atajo.")                        \
    X(KeyShift, L"Shift", L"Mayús")                                                                          \
    X(SettingsPrimaryMonitor, L"Primary monitor", L"Monitor principal")                                      \
    X(SettingsMonitorOff, L"%ls - off", L"%ls - apagado")                                                    \
    X(SettingsMonitorAbsent, L"%ls (not connected)", L"%ls (no conectado)")                                  \
    X(SettingsEdgeRight, L"Right", L"Derecho")                                                               \
    X(SettingsEdgeLeft, L"Left", L"Izquierdo")                                                               \
    X(SettingsThemeFollow, L"Follow Windows", L"Igual que Windows")                                          \
    X(SettingsThemeDark, L"Always dark", L"Siempre oscuro")                                                  \
    X(SettingsThemeLight, L"Always light", L"Siempre claro")                                                 \
    X(SettingsDelayInstant, L"Instant", L"Inmediato")                                                        \
    X(SettingsDelayMs, L"%d ms", L"%d ms")                                                                   \
    X(SettingsListEntry, L"%zu: %ls", L"%zu: %ls")                                                           \
    X(TypeQuickActions, L"Quick Actions", L"Acciones rápidas")                                               \
    X(TypeMedia, L"Media (auto-detect)", L"Multimedia (automático)")                                         \
    X(TypeBrightness, L"Brightness (monitors)", L"Brillo (monitores)")                                       \
    X(TypeLyrics, L"Lyrics (auto-detect)", L"Letras (automático)")                                           \
    X(TypeVolume, L"Volume (audio output)", L"Volumen (salida de audio)")                                    \
    X(TypeDisplays, L"Displays (switch monitors)", L"Pantallas (cambiar monitores)")                         \
    X(TypeRecent, L"Recent files (a folder)", L"Archivos recientes (una carpeta)")

namespace Strings {

enum class Id : int {
#define EDGEDECK_STRING_ID(id, en, es) id,
    EDGEDECK_STRINGS(EDGEDECK_STRING_ID)
#undef EDGEDECK_STRING_ID
    Count
};

inline constexpr int kLanguageCount = 2;

Language Current();
void SetCurrent(Language language);

// "en", "es": what config.txt holds. Anything else read back is English.
const wchar_t* Code(Language language);
Language FromCode(const std::wstring& code);

// The language EdgeDeck speaks for a Windows display language (a LANGID): the one with the same
// primary language - any Spanish, from Spain or Mexico or anywhere - else English. A first start
// uses it, before there is a choice in config.txt to read; FromWindows asks Windows for the user's.
Language ForWindowsLanguage(unsigned short langId);
Language FromWindows();

// A language's name in itself ("English", "Español"), which is how a language list is shown: a
// person looking for their own language recognises it written their own way.
const wchar_t* NativeName(Language language);

const wchar_t* Get(Id id);
const wchar_t* Get(Id id, Language language);

// Get(id) used as a printf format with the arguments given.
std::wstring Format(Id id, ...);

} // namespace Strings

// The text `name` in the current language.
#define TR(name) ::Strings::Get(::Strings::Id::name)
