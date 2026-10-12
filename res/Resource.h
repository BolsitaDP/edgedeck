// Resource ids shared by the resource script (EdgeDeck.rc) and the code that loads them.
//
// They live in one header on purpose. The script used to name its icon IDI_EDGEDECK while the
// code asked for 101, and nothing defined IDI_EDGEDECK: the resource compiler quietly takes an
// unknown identifier as a *name*, so the icon was stored under the string "IDI_EDGEDECK", the
// code never found 101, and the notification area showed the generic application icon.
#pragma once

#define IDI_EDGEDECK 101
