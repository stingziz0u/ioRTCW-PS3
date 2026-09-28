# CLAUDE.md — ioRTWC-PS3

Port de **iortcw** (Return to Castle Wolfenstein, **solo single player**) a
PS3 homebrew con PSL1GHT. Base de la capa de plataforma: **IoQuake3-PS3**
(`github.com/Mayo1970/IoQuake3-PS3`, GPLv2+), que corre Quake 3 en hardware
con su capa GL→RSX. El motor es iortcw SP (`github.com/iortcw/iortcw`,
carpeta `SP/`, GPLv3 + términos adicionales de id).

## Reglas de trabajo

- **Comunicación en español**, informal, iteración rápida; correcciones
  cortas antes que explicaciones largas.
- **División del trabajo**: el coding lo hace Claude; el debugging y el testeo
  en hardware real los hace el dueño del repo. **Nada de RPCS3.** Siempre pasar
  **comandos concretos y en orden**, listos para copiar, nunca "fijate si…".
- **No correr tests largos por él.** Claude puede compilar y linkear para
  verificar (toolchain en la sesión), pero no "probar el juego". Los crashes se
  investigan **leyendo el código**, no reproduciendo.
- **Entorno del dueño**: WSL Ubuntu sobre Windows 11, PS3 Super Slim con HEN +
  webMAN. Instalación de PKG y transferencia de datos **por FTP, siempre**.
- **Ruta del proyecto en WSL: `~/ioRTWC-PS3`.** Todos los comandos empiezan con
  `cd ~/ioRTWC-PS3`. Nunca asumir en qué carpeta está parado.
- Descargas desde el chat llegan a `/mnt/c/Users/Ren/Downloads/`. Los comandos
  leen los archivos **directo de Descargas**. Windows renombra los repetidos
  (`archivo (1).patch`): **cada archivo reenviado lleva un nombre nuevo**
  (numerados: `iortcw-ps3-1.patch`, `-2`, …).
- **Nada se sube al repo hasta tener una 1.0 probada en consola.** Ni push (ni
  a ramas `claude/…`), ni tags, ni releases. El stop hook que pide push se
  contesta en una línea: pendiente de la prueba en consola. Cuando la 1.0 anda,
  se sube todo y **el dueño crea la release** con el PKG desde la web.
- **Commits**: sin la línea `Claude-Session:` ni links de sesión; nunca
  identificadores de modelo en commits, README o código. El email público del
  dueño es `323042592+stingziz0u@users.noreply.github.com` (no editar archivos
  desde la web de GitHub: firma con el email real).
- **Compilar solo lo que hace falta para la prueba.**
- **PKG que prepara todo**: el PKG crea las carpetas que el usuario tiene que
  llenar (`main/`), **vacías, sin `.txt` de relleno**. Logs siempre dentro del
  USRDIR del juego.
- **Video**: resolución interna elegible, escalada a 720p por el RSX, con
  opción 16:9 en el menú de video (patrón "Video Settings").
- **Todo cambio a archivos de upstream va entre `#ifdef __PS3__`**; los archivos
  `ps3_*` y `code/{gl,audio,input,renderer}/` son propios del port.

## Datos del proyecto

| | |
|---|---|
| Título en el XMB | `Return to Castle Wolfenstein` |
| APPID / TITLE_ID | `IORTCWPS3` (9 caracteres, obligatorio) |
| Ícono | `ICON0.PNG` (320×176 RGBA) |
| Datos del usuario | versión Steam 1.41: `pak0.pk3`, `sp_pak1.pk3` … `sp_pak4.pk3` en `USRDIR/main/` (`files.c` exige los cuatro `sp_pak`: `NUM_SP_PAKS 4`) |
| Toolchain del dueño | `ppu-gcc` 7.2.0 (ps3toolchain), `/usr/local/ps3dev` |

## Build

- `make STAGE=1`: headless (motor + server + qagame, sin video/input/audio).
  Recorre todos los `maps/*.bsp`, loguea el hunk que usa cada uno y sale.
  PKG: `iortcw-ps3-stage1.pkg`.
- `make` (STAGE=2): el juego. PKG `iortcw-ps3-<VERSION>.pkg`. Todavía sin
  niebla, sin Video Settings (dibuja directo a 720p) y sin OSK.
- `DEBUG=1` en cualquiera. Cada variante tiene su `obj/<variante>`: no hace
  falta `make clean` al cambiar.
- `make getlog PS3IP=…` baja `USRDIR/iortcw-ps3.log` por FTP.
- El link corre `make check`: `.got` < 0x10000 y cero stubs `r2off`.
- **Toolchain en una sesión nueva de la nube**: `bash tools/setup-cloud-toolchain.sh`
  (~25 min; mismo gcc 7.2 que el dueño, fuentes desde mirrors de GitHub).

## Decisiones del port

- **Sin red**: `code/sys/ps3_net_null.c` reemplaza `net_ip.c`. Cliente y
  servidor locales usan el loopback del motor; no se carga el módulo de red.
- Módulos linkeados: `Sys_LoadGameDll` (`ps3_sys.c`) busca por nombre en una
  tabla; `VM_Create` (`vm.c`, `#ifdef __PS3__`) va directo ahí sin buscar
  archivos. Cada módulo: `ld -r -T ps3_module.ld` + `objcopy` (Makefile).
- Estático de los módulos (medido): qagame ~6.6 MB de bss, cgame ~17 MB
  (`cgModelInfo` 5.6 MB, `cg_entities` 4.1 MB), ui ~2.5 MB. Candidatos a
  achicar si falta memoria.
- **Stage 1 en hardware (28 sep 2026)**: 31 de 32 mapas cargan (norway es un
  mapa de prueba sin terminar, su script falla igual en PC). Memoria libre al
  arrancar el stage 1: ~191 MB. Hunk del lado servidor: 20–31 MB por mapa
  (máx. swf 30.4 MB, forest 31.7 MB con el high). Carga 1–3 s por mapa. Sin
  fugas en 31 cambios de mapa (libre estable 36–42 MB con hunk de 143).
- Stack del hilo del juego: 2 MB. El peor frame medido
  (`-fstack-usage`) es `CM_GeneratePatchCollide`, 325 KB.
- `ps3_setjmp.S` + `sys/include/setjmp.h` (de Mayo): el setjmp de newlib pisa
  el stack. Nunca sacarlos.

- **Memoria del stage 2** (decisiones que la hacen entrar):
  - `MAX_RAW_STREAMS 13` (`snd_local.h`): música + 12 voces como el RTCW
    original. El valor de iortcw (257, pensado para VoIP) eran 32 MB de bss.
  - Buffer IO del RSX de 8 MB (`ps3_glimp.c`), no 32: ps3gl tiene todo en la
    memoria local del RSX y del IO solo usa el command buffer de 1 MB.
  - `com_soundMegs` con tope `PS3_MAX_COMSOUNDMEGS` = 6 (18 MB) en
    `snd_mem.c`; el default de iortcw (32) pedía ~100 MB.
  - Hunk calculado al arrancar con la memoria libre medida (`ps3_main.c`).
- EBOOT del stage 2: ~48 MB (bss ~42 MB, mayormente los módulos del juego).
- **TOC**: botlib, server, client, jpeg, splines y zlib van con
  `-mminimal-toc` (`MINTOC_DIRS` en el Makefile). Sin eso el `.got` llegaba a
  0xfa80 (1,4 KB del límite); con eso, 0x87c0.
- GL: `qgl_ps3.c` pone un stub que loguea "not implemented" en cada entrada
  GL y encima conecta las de ps3gl. Si falta algo, sale en el log.
- Pad (`ps3_input.c`): en juego todo es tecla bindeable (`K_JOY1..15`); en
  menús/consola X = Enter, O = Escape, cruceta **y stick izq.** = flechas con
  auto-repeat, stick der. = puntero con aceleración. SELECT solo = notebook
  (al soltar); SELECT+TRIÁNGULO = consola.
- Menús con pad (`ui/ui_shared.c`, `#ifdef __PS3__`): un ítem de texto solo
  toma el foco con el cursor encima, así que la navegación apunta al centro
  del ítem y **mueve el cursor ahí** (foco y puntero no se separan). X sin
  foco enfoca el primer ítem, y si es el único del menú (el "continuar" del
  briefing después de cargar) lo activa. Arreglados dos bugs de upstream en
  `Menu_SetNext/PrevCursorItem` (saltaban el primer/último ítem y podían
  leer `items[-1]`).
- **Screen fit** (overscan): cvar `vid_ps3_screenfit` (70–100, default 88,
  latched: aplica en el próximo arranque o `vid_restart`). El motor ve un
  rect centrado de ese % del 1280×720 (`ps3gl_set_fit`: viewport y scissor
  desplazados); el borde se limpia a negro en cada frame. Va a ir al menú
  Video Settings.

## Restricciones heredadas de IoQuake3-PS3 (no romper)

- **TOC única de 64 KB**: `ppu-gcc` 7.2 no tiene `-mcmodel`. Linkear con
  `-Wl,--no-multi-toc` (nunca sacarlo). Si se pasa, `-mminimal-toc` en los
  objetos pesados. Verificar después de cada link:
  `ppu-nm <elf> | grep -c r2off` = 0 y `.got` < 0x10000.
- **Memoria**: ~145 MB libres al arrancar (de ~213 MB del proceso se van el
  buffer IO del RSX y los segmentos del EBOOT). lv2 **se cuelga** en vez de
  devolver NULL cuando un malloc grande no entra. El mínimo de hunk de iortcw
  SP (128 MB) no entra: hay que medir por mapa.
- **Sin JIT ni carga de DLL**: qagame, cgame y ui van linkeados estáticos,
  cada uno con `ld -r` + `objcopy` dejando globales solo sus entradas.
- `-O2` global (`-O3` rompe el arranque), `-mno-altivec` global.
- `-lrt -llv2` en el link, `SYS_PROCESS_PARAM`, callback de sysutil, todos los
  caminos de salida con shutdown.
- No hay `/tmp`, `chdir` ni rutas relativas en PS3: todo absoluto bajo
  `/dev_hdd0/game/IORTCWPS3/USRDIR/`.
