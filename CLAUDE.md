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
- **Entrega de cambios: parches comunes, no builds.** Hasta la 1.0 el dueño
  no hace commits: su árbol `~/ioRTWC-PS3` se modifica solo con parches
  numerados (`iortcw-ps3-N.patch`, unified diff) que aplica y compila él:
  `cd ~/ioRTWC-PS3 && patch -p1 --dry-run < /mnt/c/Users/Ren/Downloads/iortcw-ps3-N.patch && patch -p1 < /mnt/c/Users/Ren/Downloads/iortcw-ps3-N.patch && make -j4 && make verify`.
  Claude lleva el historial en su copia (commits locales) y genera cada
  parche contra el estado del parche anterior; compila solo para chequear y
  prueba el parche con `patch -p1 --dry-run` sobre una copia. No manda PKG ni
  bundles.
- **Cada parche sube `PATCHLEVEL`** (archivo en la raíz). Sale en la primera
  línea del log (`patch N`) y en `USRDIR/VERSION.TXT`: así se sabe qué
  build corre la consola. `make verify` muestra el nivel del árbol y del PKG.
- **FTP lo maneja el dueño con FileZilla**: instala el PKG y baja los logs
  (`USRDIR/iortcw-ps3.log`) él. No pasarle comandos de `curl`/FTP.
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
- `make` (STAGE=2): el juego. PKG `iortcw-ps3-<VERSION>.pkg` (1.0 desde
  el patch 19). Sin el diagnóstico de las pruebas: `make DIAG=1` (PKG
  `...-diag`, obj aparte) prende `cg_drawFPS 1`, las líneas `[fps]`/`[prof]`/
  `[mem]` cada 30 s, `[hitch]`, `[pad]`, `[lock]`, `[fog]`, `[snd]`, `[cam]` y
  `PS3GL_TEXIMG` (todo con `#ifdef PS3_DIAG`); MAPWALK y DEBUG lo tienen
  siempre. El perfil mide igual en todas (es barato), solo no se loguea.
  `ps3_cfgver 1`: apaga una vez el `cg_drawFPS 1` que las builds de prueba
  dejaron en `wolfconfig.cfg`. Quedan en todas: arranque, `[osk]`, `[save]`,
  `[smp]` y errores.
- `make MAPWALK=1`: el juego completo que carga cada mapa con `spmap` (con
  renderer, cgame y sonido), pasa la pantalla de misión como el jugador,
  juega 6 s desde el inicio y loguea por mapa memoria (hunk, malloc, texturas
  en el RSX), fps y el perfil del frame; al final sale solo. Si un mapa
  cuelga lv2 por memoria, la última línea `[walk] ----` dice cuál. PKG
  `iortcw-ps3-mapwalk.pkg` (mismo APPID: después reinstalar el del juego).
  `make MAPWALK=1 WALKMAPS="village2 dark"`: solo esos mapas, 20 s cada uno
  (va en `ps3_rev.h`, así cambiar la lista recompila `ps3_main.c`).
  `make MAPWALK=1 WALKSECS=90` (patch 16): prueba larga de scripts y
  cinemáticas. Cada mapa 90 s con `god` (`sv_cheats 1` después de cargar con
  `spmap`; el `god` va a los 2 s de juego: antes el game todavía no vio la
  cvar y contesta "Cheats are not enabled"), los `cutscene*` hasta que su
  script carga el nivel siguiente (máx. 300 s) y el walk sigue ese nivel
  como el jugador (carga, pantalla de misión, 5 s: `[walk] -> mapa`). Hasta
  pasar esa pantalla el game deja `g_reloading` puesto y `SV_Map_f` ignora
  cualquier `spmap` (el patch 16 fallaba así después de cutscene1); igual
  antes de cada `spmap` el walk pone `g_reloading 0` como el menú principal.
  Si el mapa se recarga solo (objetivo fallado) lo espera y sigue (hasta 3).
  Por mapa: cámaras que arrancaron (`[cam] start` de `CG_StartCamera`) y
  líneas de consola con warning/error (contadas en `PS3_LogRaw`, sin las dos
  de los shader scripts del pk3 que salen en cada arranque del renderer).
  cutscene19 termina el juego (`G_EndGame` → `ERR_ENDGAME` → créditos):
  el walk lo ve por `cls.endgamemenu` y deja 5 s para que suba el menú. El
  patch 17 mandaba el `spmap` siguiente en el mismo frame: `SV_SpawnServer`
  bajaba la ui y `CL_Frame` hacía `VM_Call(uivm = NULL, UIMENU_ENDGAME)`
  → error fatal (bug de upstream, ahora con `if (uivm)` en `cl_main.c`).
  Medido (patch 17, 15 mapas): 59,7–59,9 fps en 90 s, `god` en todos,
  cámaras de intro y de script corren (end 18, cutscene14 32), cutscene1 →
  escape1 (184 s), cutscene11 → norway (100 s), cutscene14 → dam (213 s),
  cada uno cargado y jugado después de su pantalla de misión; cero
  warnings propios (solo falta `osa2.tga` en los pk3). ~1 hora los 32
  mapas; con `WALKMAPS` + `WALKSECS` corre solo esos mapas en modo largo.
  Patch 18, los 17 restantes + cutscene19: 18 de 18 OK, 59,8–59,9 fps,
  cutscene6 → forest, cutscene9 → sfm, cutscene19 → créditos. Con eso los
  32 mapas y las 6 cinemáticas están probados en consola: base de la 1.0.
- `DEBUG=1` en cualquiera. Cada variante tiene su `obj/<variante>`: no hace
  falta `make clean` al cambiar.
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
- **Stage 1 en hardware (28 sep 2026)**: 31 de 32 mapas cargan. norway (misión
  5) fallaba porque el stage 1 usaba `map`, que fuerza FFA y borra las
  entidades `notfree` que su script usa; el juego usa `spmap` (single
  player) y el stage 1 ahora también. Memoria libre al
  arrancar el stage 1: ~191 MB. Hunk del lado servidor: 20–31 MB por mapa
  (máx. swf 30.4 MB, forest 31.7 MB con el high). Carga 1–3 s por mapa. Sin
  fugas en 31 cambios de mapa (libre estable 36–42 MB con hunk de 143).
- **Mapwalk en hardware (29 sep 2026, patch 6)**: los 32 mapas cargan y
  juegan (norway incluido). Carga 21–29 s por mapa. Peor caso de memoria: hunk
  libre 21 MB (swf), malloc libre ~29 MB, texturas en el RSX 69–94 MB (dam),
  ninguna textura sin lugar. Fps en los 6 s del inicio: la mayoría 55–59;
  flojos: end 48, village1 47, assault 44, xlabs 43, dark 36 y **village2 4**
  (cgame 211 ms por frame). En juego (escape1) los frames lentos son de CPU,
  no de GPU (flipwait ~0): cgame + front end ~10 ms y backend ~10 ms con
  ~160 draws, ~15k vértices y ~42k índices; más tirones de 50–500 ms en
  cgame/sv (carga de recursos a mitad del juego).
- Perfil por syscall (`VM_DllSyscall` en `vm.c` → `PS3_ProfSyscall`): tiempo
  y llamadas por syscall de los módulos game y cgame (nombres en
  `ps3_prof_names.h`, generado de los enums de `g_public.h`/`cg_public.h`);
  cada 30 s las 6 más caras y el "own code" del módulo. Una syscall de más de
  10 ms sale como `[hitch]` con lo que cargaba (modelo, sonido, shader). En
  el backend, `gldraw` (ps3gl DrawElements) y `glcopy` (copia de vértices al
  RSX) separan ps3gl del renderer de iortcw; `anim` (skinning de modelos
  MDS, `RB_SurfaceAnim`) y `shade` (`RB_EndSurface`, contiene gldraw)
  parten el resto del backend.
- Medido con el perfil (patch 7): cada archivo abierto dentro de un pk3
  cuesta ~12 ms (acceso del HDD); los modelos MDS tardan 80–400 ms en
  registrarse. En juego pesado (dark, xlabs, escape1): cgame ~8–12 ms
  (`CG_R_LERPTAG` 1–2 ms con ~100 llamadas, `CG_CM_MARKFRAGMENTS` hasta
  4 ms en xlabs), backend 8–13 ms (ps3gl 2–4 ms de eso), sv ~2 ms. El volcado `[menu]` quedó
  detrás de `PS3_MENU_DUMP`.
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
  - **Sonido a 22050 Hz** (`ps3_snd.c`): el mezclador corre a la frecuencia
    de los sonidos de RTCW y el hilo de audio convierte a 48 kHz (lineal,
    22050/48000 = 441/960 exacto, sin deriva). A 48 kHz cada sonido ocupaba
    2,2× en el caché, que se llenaba y releía del HDD a mitad del juego
    (`[hitch] CG_S_STARTSOUND` de ~25 ms, village2 a 2 fps con
    `CG_S_ADDLOOPINGSOUND` de 440 ms por frame). Además los sonidos mono de
    2 s o más (voces, loops ambientales) van en ADPCM de 4 bits
    (`snd_mem.c`); los efectos cortos quedan en 16 bits. Ring de 2048 pares
    (~93 ms de latencia, como antes). Bug de upstream que el ADPCM destapó
    (patch 9): `ResampleSfxRaw` hacía `LittleShort` sobre muestras que el
    codec wav ya había pasado al orden del host → en big-endian, ruido.
  - Caché de 8 chunks ADPCM decodificados (`snd_mix.c`, clave = puntero del
    chunk; `SND_malloc` lo olvida al reusarlo): upstream tenía uno solo
    compartido y con varias voces/loops redecodificaba 4096 muestras en cada
    pasada.
  - Al bajar de frecuencia (sonidos o streams de 44,1 kHz → 22050) se
    promedian las muestras que cubre cada salida en vez de tomar una
    (`PS3_DecimSample16` en `snd_mem.c`, `S_Base_RawSamples`): sin filtro
    daba aliasing metálico. Cada sonido que no es 22k mono y cada música
    salen en el log como `[snd] ...` (patch 12).
  - Hunk calculado al arrancar con la memoria libre medida (`ps3_main.c`).
- EBOOT del stage 2: ~48 MB (bss ~42 MB, mayormente los módulos del juego).
- **Ring de vértices de ps3gl en memoria principal** (patch 10): 2 × 2 MB en
  la mitad alta del buffer IO de 8 MB (el command buffer usa el primer MB),
  atributos con `GCM_LOCATION_CELL`. El PPU escribía cada vértice sin caché
  en la memoria local del RSX (~150 ns por vértice, `glcopy` 2,3 ms por
  frame). Si el IO no alcanza vuelve a la memoria del RSX; el log dice dónde
  quedó (`ps3gl: vertex ring ...`). Libera 8 MB de memoria del RSX.
  Medido en escape1 (patch 10): `glcopy` 2,3 → 0,3 ms, backend de los frames
  lentos ~10 → ~7,5 ms.
- **Trigonometría de los huesos** (`tr_animation.c`, patch 11): `sin`/`cos`
  de doble precisión de newlib (10–14 por hueso en `R_CalcBone`/`Lerp`) →
  polinomio en precisión simple (`PS3_SinTurns`, error ≤ 4e-6); también
  `AnglesToAxis` local. Pega en `LerpTag` (cgame) y en `anim` (backend).
- **Sombras de personajes** (`cg_players.c`, patch 12): con `cg_shadows 1`
  cada personaje proyecta en cada frame hasta 3 marcas (torso y pies:
  `CM_MarkFragments` + trazas + `LerpTag`); en xlabs, 4 ms. En PS3 se
  saltean las de personajes fuera del campo de visión (fov/2 + 25°, salvo a
  menos de 96 unidades). Medido (patch 11): village2 59 fps, dark 50, xlabs
  49; las escenas lentas son el patio exterior de escape1 (2 escenas por
  frame por el portal sky, `cg_skybox`) y las salas con fuego (partículas,
  luces dinámicas, marcas).
- **Back end en el 2º hilo del PPU (`r_smp`, patch 13)**: medido en el
  patio de escape1 (patch 12) el frame lento es ~20 ms todo de CPU en serie
  (sv 2 + cgame/front end 7 + back end 10: shade 5,8, anim 2); apagar portal
  sky o luces dinámicas no cambió nada. Ahora el front end arma el frame
  N+1 mientras el hilo `render` (`ps3_glimp.c`, prioridad 1000, stack
  512 KB) corre el back end del N, como el `r_smp` del RTCW original: dos
  `backEndData` (`r_smpBackEndData`, +1,9 MB de hunk), `R_InitNextFrame`
  los alterna, `R_IssueRenderCommands` espera el anterior
  (`GLimp_FrontEndSleep`, fase `rwait` del perfil) y despierta al hilo;
  `R_IssuePendingRenderCommands` además espera que termine (todo GL del
  front end va después). `PS3_RSX_BeginFrame` (flip, fence, borde) pasó de
  `RE_BeginFrame` a `RB_DrawBuffer`: corre en el hilo de render. Datos
  compartidos resueltos: `dlightBits[2]` en las superficies del mundo
  (índice `tr.smpFrame` / `backEnd.smpFrame`); huesos MDS: `R_GetBoneTag`
  (LerpTag de cgame) y `RB_SurfaceAnim` usan las estáticas de
  `tr_animation.c`; el hilo del juego usa una segunda copia del archivo
  (`tr_animation_front.c`: lo incluye con los nombres exportados
  renombrados, `PS3_R_GetBoneTagFront`). El lock del patch 13 hacía que se
  esperaran y se vaciaran el caché de huesos (LerpTag 0,6 → 2,5 ms); sync en `R_FindShader` y
  `RE_RegisterShaderFromImage` al crear un shader (sube imágenes y reordena
  `tr.sortedShaders`), `RE_LoadWorldMap` y `RB_ZombieFXAddNewHit`; el back
  end lee `backEnd.refdef` (no `tr.refdef`) para fade de entidades, snooper
  y `RDF_DRAWSKYBOX` del portal sky (upstream leía `drawskyboxportal`, que
  el front end ya cambió); los videoMap de shaders se decodifican y suben
  en el hilo del juego con el render quieto (`R_SMP_RunVideoMaps`, un
  frame de atraso: el decode usa archivos y hunk temporal);
  `SurfIsOffscreen` (usa `tess`) devuelve qfalse como en el original;
  `R_SortDrawSurfs` ya no apaga/prende fog (GL) salvo con `r_debugSurface`;
  sin chequeo de `glGetError` en `RE_BeginFrame` (siempre OK en ps3gl y
  forzaba sync). Red de seguridad: `PS3GL_FRONT_CHECK` en las entradas
  principales de ps3gl compara el stack con el del hilo de render; una
  llamada de otro hilo con el back end corriendo loguea `[smp] <función>
  from the game thread` y espera. Latencia: un frame más. `r_smp 0`
  (cvar archivada y latched, `wolfconfig.cfg`) vuelve al back end en el
  hilo del juego. Con SMP la línea `[prof] avg (smp)`: backend, anim,
  shade, gl*, flipwait y fencewait son del hilo de render (en paralelo);
  `cl` incluye `rwait`.
  Medido en hardware (patch 13, escape1 entero): 59,9 fps en casi todas
  las ventanas de 30 s, la peor 58,9 (combate: front end sv 3–4 + cgame
  ~13 ms, `LERPTAG` 1,5 ms con ~100 llamadas); sin líneas `[smp]` de
  error. Tirones que quedan (todos del hilo del juego): `CG_S_REGISTERSOUND`
  20–85 ms (sonido leído del pk3 a mitad del juego), `CG_R_GETMODELSHADER`
  17–29 ms (`CG_Explodef` crea la variante sin lightmap del shader al
  romperse algo) y 20–30 ms en `snd` cada vez que la música vuelve a
  empezar (reabre el stream: `FS_FOpenFileRead` único = `unzOpen` del pk3).
- **Niebla GL en ps3gl** (patch 14): RTCW pone la niebla con `glFog*` +
  `GL_FOG` (`R_Fog`, `SetIteratorFog`). No hay compilador Cg en Linux (el
  libCg de NVIDIA no se consigue), así que los shaders nuevos están en
  assembler NV40 escrito a mano (`code/gl/shaders/asm/*.vpa/*.fpa`) y
  `assemble_shaders.sh` (cgcomp -a) regenera `ps3gl_shader_data.h`
  (verificado: el `q3_vp.vcg` y el `q3_fp_modulate.fcg` reescritos así dan
  binarios idénticos a los originales). El VP calcula el factor con la w de
  clip (distancia sobre el eje de la vista): lineal `d·A+B` o exp
  `2^(d·C)`, mezclados por `fogParams.w`, y lo manda con el color en
  `texcoord[2]`; con `GL_FOG` prendido se usan los 7 FP `_fog` (mismo modo
  + `lerp(fogColor, color, sat(f))`, alfa intacto). NV40: una entrada
  interpolada por instrucción. Constantes `fogParams`/`fogColor` solo al
  cambiar (RTCW las setea en cada batch) y una vez por frame. El log dice
  `ps3gl: fog shaders loaded` y las primeras nieblas como `[fog] ...`.
  Con `r_smp` el back end usa la niebla copiada en su `RC_DRAW_SURFS`
  (`backEnd.fog`/`fogNum`; en `tr_backend.c`, `tr_shade.c`, `tr_sky.c` un
  `#define glfogsettings backEnd.fog`), porque el front end ya la está
  fundiendo para el frame siguiente. `RB_SetGL2D` pone `fogIsOn` en falso
  junto con el `glDisable(GL_FOG)`.
- **Recorte por niebla** (patch 14, `R_SetupFogCull` en `tr_main.c`): con
  niebla lineal del mapa (`clearscreen`) lo que está más lejos que el fin de
  la niebla sale del color de fondo; un 5º plano (`viewParms.fogPlane`) lo
  saca en `R_RecursiveWorldNode` (bit 16), `R_CullLocalBox` y
  `R_CullPointAndRadius`. No en transiciones de niebla, portales, snooper
  ni con `r_zfar`.
- **Lock-on** (patch 14, `CL_LockOn` en `cl_input.c`, `+lockon` en L2):
  mientras se mantiene, la vista gira hacia el enemigo visible más cercano
  a la mira (`cl_lockonAngle` 30°, `cl_lockonRange` 3000) y lo sigue
  mientras viva y no se esconda más de 400 ms; si muere, toma el siguiente.
  Giro suave `1 - e^(-cl_lockonSpeed·dt)` (12/s) y el stick sigue sumando.
  Enemigos: `ET_PLAYER` con `teamNum` 0 (nazis) o 2 (monstruos) sin
  `EF_DEAD`; el jugador es aliado (1). Posición del snapshot llevada al
  tiempo de dibujo con la velocidad, a la altura del pecho; visibilidad con
  `CM_BoxTrace` contra el mundo. Cada blanco nuevo sale como `[lock]`.
  Binds: L2 `+lockon`, R3 `weapalt` (antes binoculares, que quedan sin
  botón); `in_ps3binds 2` migra una sola vez las configs con los binds
  viejos por defecto.
- **Conversiones float → int del back end** (patch 15): `ri.ftol` en PS3 es
  `lrintf` de newlib (software, por puntero) y en el PPU leer un int
  recién convertido traba ~40 ciclos (load-hit-store). `RB_CalcDiffuseColor`
  (luz de los personajes, 3 por vértice) y los colores de
  `ProjectDlightTexture` (luces dinámicas, 3 por vértice por luz) convierten
  todo con `PS3_StoreRoundedInt` (fctiw + stfiwx, `tr_local.h`) y empaquetan
  los bytes en otra pasada. Medido antes (patch 14, escape1 quieto a 45 fps):
  el back end 22 ms (shade 12,3, anim 5,8, 208 draws, 95k índices) era el
  límite; el front end esperaba 9 ms. El perfil suma `surf` (armado de
  superficies, incluye anim), `bones` (`R_CalcBones`), `colors` y `tcoords`
  (por etapa, dentro de shade).
  Medido después (patch 15, la misma escena de escape1 con ~195 draws y
  ~70k índices): back end 22 → 13–14 ms (shade 13,5 → 5,9, anim 6,5 → 3,2,
  bones 1,6, colors 1,5), `LERPTAG` 2,5 → 1,3 ms; 59,1–59,9 fps en todas las
  ventanas de juego. Lo que queda en combate es el front end: sv 2–4 +
  cgame ~11–14 ms (`RENDERSCENE` ~3, `LERPTAG` ~1,3 con ~105 llamadas).
- **Mapwalk patch 14** (forest, village1, rocket, assault, norway): 59,5–59,7
  fps con niebla lineal y exp activa.
- **Música**: al volver a empezar el loop, `S_UpdateBackgroundTrack`
  rebobina el wav abierto (`FS_Seek` al primer sample) en vez de reabrirlo
  (reabrir = `unzOpen` del pk3, 20–30 ms en un frame).
- **TOC**: botlib, server, client, jpeg, splines y zlib van con
  `-mminimal-toc` (`MINTOC_DIRS` en el Makefile). Sin eso el `.got` llegaba a
  0xfa80 (1,4 KB del límite); con eso, 0x87c0.
- GL: `qgl_ps3.c` pone un stub que loguea "not implemented" en cada entrada
  GL y encima conecta las de ps3gl. Si falta algo, sale en el log.
- Pad (`ps3_input.c`): en juego todo es tecla bindeable (`K_JOY1..15`); en
  menús/consola X = Enter, O = Escape, cruceta **y stick izq.** = flechas con
  auto-repeat, stick der. = puntero con aceleración, □ = click del puntero
  (K_MOUSE1). L2 = lock-on, R3 = fuego alternativo/mira, cruceta arriba =
  patada. SELECT tocado = notebook (al soltar), SELECT mantenido 300 ms =
  `K_JOY16` ("SELECT (HOLD)") = binoculares (`+zoom`) hasta soltarlo, solo
  en juego o esperando tecla en Controles; SELECT+TRIÁNGULO = consola.
  `in_ps3binds 4` (patch 17) devuelve `+kick` a la cruceta arriba si tenía
  el `+zoom` del patch 16.
- Controles con pad (patch 16): los menús muestran los nombres del DS3
  (`Key_KeynumToStringBuf` en `cl_ui.c`: CROSS, L2, D-PAD UP, SELECT
  (HOLD)…; la config
  sigue guardando `JOYn`). Mientras un ítem de Controles espera tecla,
  `ui_shared.c` pone `ui_ps3bindwait 1` y `pad_key_for` manda cada botón
  como su tecla de juego (no Enter/Escape/flechas); START cancela.
- Menús con pad (`ui/ui_shared.c`, `#ifdef __PS3__`): los menús de RTCW
  son para mouse (un ítem de texto solo toma el foco con el cursor encima,
  los submenús son menuDefs aparte y solo el click pasa el foco de un menú a
  otro). En PS3 la cruceta mueve el foco **espacialmente** al ítem más
  cercano en esa dirección, en cualquier menú visible (menos si el enfocado
  es popup), con wrap en la misma fila/columna. El foco se pone directo y el
  cursor se mueve al ítem (corren los mouseEnter/Exit); el menú dueño pasa a
  ser el enfocado y los demás pierden el resaltado, así X actúa siempre
  sobre lo resaltado. Cuando el puntero (stick der.) se mueve, el ítem
  usable debajo toma el foco, en el menú que sea (`Menus_PadPointerMoved`,
  llamado desde `_UI_MouseEvent`); sobre nada usable el resaltado queda
  donde estaba. X nunca actúa sobre un dibujo o etiqueta resaltado por el
  hover. Los menús del juego (`ingame*`) tienen `outOfBoundsClick`: el pad
  pasa entre ellos, y X sobre un ítem cierra los menús OOB que no lo
  contienen (como el click afuera). Botones que aparecen con el hover (la
  flecha de continuar del "pregame": `but2` visible con mouseEnter muestra
  `but2_alt`, oculto, con la acción) cuentan como un ítem: el pad los
  enfoca, el puntero los revela y X ejecuta la acción del oculto. X en un
  slider con el puntero movido a mano: a la izquierda del knob baja. Un ítem oculto con `hide` pierde el foco; después de X (o de O) si el menú enfocado quedó sin foco
  se enfoca su primer ítem. X sin foco: si hay un solo ítem con acción (la
  flecha de "continuar") lo activa, si no enfoca el primero. En una lista,
  X = doble click, y arriba/abajo en los extremos salen de la lista.
  Izq./der. en multi/slider/sí-no cambian el valor (upstream). El menú
  "pregame" (escudo de la OSA) se comía todas las teclas menos el click
  (`cl_keys.c`): en PS3 deja pasar Enter y flechas. Arreglados dos bugs de
  upstream en `Menu_SetNext/PrevCursorItem` (saltaban el primer/último ítem
  y podían leer `items[-1]`). Diagnóstico: cada menú loguea su estructura
  (ítems, rects, acciones) la primera vez que se abre (`[menu]`) y cada X
  loguea sobre qué actuó (`[pad]`): los scripts de menú están en los pk3
  del usuario, no en el repo.
- Mira con el stick (`CL_JoystickMove`): se saca la zona muerta del pad y
  pasa por una curva (0.25·m + 0.75·m²). Velocidad máxima = 30 °/s ×
  `sensitivity`; el slider del menú (texto "Mouse" → "Gamepad") va de 1 a
  10 en PS3 (5 = 150 °/s, el medio); valores > 10 del rango viejo vuelven a
  5 en `IN_Init`. Pitch al 75 %. El zoom de miras y binoculares la baja como
  al mouse y "invertir" (`m_pitch` < 0) invierte el stick. De `j_yaw` /
  `j_pitch` solo se usa el signo.
- **Perfil del frame** (`ps3_prof.c`, API en `ps3_platform.h`): cada fase
  se mide con el timebase (`PS3_ProfTick`, sin syscall): servidor, cliente,
  cgame + front end, back end + ps3gl, espera de flip (GPU atrasada), espera
  del fence del ring de vértices, sonido, idle (`com_maxfps`) y `rwait`
  (el juego esperando al hilo de render). Cada 30 s,
  con la línea `[fps]`, salen `[prof]`: promedio de todos los frames,
  promedio de los lentos (> 20 ms) y el peor frame, con draws, vértices e
  índices de ps3gl, el pico del ring de vértices y `[mem]` (hunk, malloc,
  MB de texturas en el RSX y texturas que no entraron).
- Guardar partida sin teclado: X en el campo del nombre abre el teclado del
  XMB (`ps3_osk.c`, receta de CrispyCell: container de 8 MB o 4 MB, callback
  en SLOT1, `oskLoadAsync`, panel de ancho completo convertido a ASCII, sin
  `"`, `;` ni `\`, máx. 24). El comando `ps3_osk <cvar> [título]` deja el
  resultado en `ps3_osk_result` (1 aceptado, 2 cancelado, 3 no abrió) y
  `Menus_PS3OskPoll` (en `Menu_PaintAll`) corre la acción del campo
  (`uiScript Savegame`) en 1 y 3. Mientras el OSK está abierto el pad no se
  lee y al cerrarse los botones apretados no cuentan. Si el nombre está
  vacío se usa `<mapa>_<nn>`, el
  primer número libre (`UI_PS3_AutoSaveName`, `ui_main.c`). Con teclado USB
  el □ (click) sigue entrando a editar. Las partidas van a
  `USRDIR/main/save/` (`rename` de librt anda).
- FPS (solo `DIAG`): `cg_drawFPS 1` forzado en la línea de comandos y en
  el log, cada 30 s, una línea `[fps]` (promedio, segundo más lento,
  peor frame, frames de más de 20 ms, mapa) desde el loop de `ps3_main.c`.
- **Screen fit** (overscan): cvar `vid_ps3_screenfit` (70–100, default 88,
  archivada, en vivo desde el patch 16). El motor ve siempre 1280×720
  (`glConfig.vidWidth/Height`) y ps3gl escala viewport y scissor al rect
  centrado de ese % (`ps3gl_map_rect`, guarda los valores GL en
  `gl_vp`/`gl_sc` y los remapea al cambiar); el borde se limpia a negro en
  cada frame. `PS3_ApplyScreenFit` lo aplica en `GLimp_Init` y en cada
  `PS3_RSX_BeginFrame` si cambió. En el menú de video el ítem
  "Fullscreen" (`r_fullscreen`, sin sentido en PS3) pasa a ser el slider
  "Screen Fit:" (`Item_ApplyHacks`). Resolución de dibujo = el rect del fit
  (1126×634 al 88 %); una resolución interna menor no ganaría nada: el
  juego está limitado por CPU.

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
