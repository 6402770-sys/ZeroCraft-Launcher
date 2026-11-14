# Lanzamiento de ZERO Launcher — guía para el dueño

Guía paso a paso de lo que **solo vos** podés hacer (cuentas, claves, decisiones) y de cómo se arma el lanzamiento.
La parte técnica (cómo funciona el empaquetado) está en `docs/zero/ARCHITECTURE.md`, sección "Packaging", y las claves en
`docs/zero/CREDENTIALS.md`. Escrita en la sesión 11 (2026-11-13).

## Estado hoy

| Pieza | Estado |
|---|---|
| Programa empaquetado (carpeta que anda sin MSYS2, zip portable) | lo arma `scripts/zero/package.sh` |
| Instalador `Setup.exe` | armado en `dist/` (NSIS instalado en la sesión 11); falta probarlo en la PC (ver paso 5) |
| Actualizaciones automáticas | apuntan a tu repo `github.com/6402770-sys/ZeroCraft-Launcher`; empiezan a funcionar cuando publiques el primer "Release" ahí |
| Iniciar sesión con Microsoft | usa la clave de Prism, **también al publicar** (decisión tuya del 13/11/2026, igual que ElyPrismLauncher, porque Azure no acepta tarjeta prepaga). El paso 2 queda opcional para más adelante |
| CurseForge | ✅ tu clave propia, comprobada el 13/11/2026 y guardada fuera del repo en `C:\Users\64027\.zero\curseforge-api-key` (la usa `--release`; los paquetes de prueba siguen con la de Prism) |
| Versión | `0.1.0` (está en `CMakeLists.txt`, línea 191) — decidila en el paso 4 |

**Las claves ya están**: Microsoft usa la de Prism (paso 2, decisión tuya) y CurseForge la tuya (paso 3). Falta probar (paso 5) y decidir (paso 4).

## Paso 1 — NSIS (para el instalador)

Hecho: NSIS 3.13 quedó instalado en MSYS2 (sesión 11, con tu OK) y `scripts/zero/package.sh` ya arma el `Setup.exe`.

## Paso 2 — Tu propia clave de Microsoft (OPCIONAL, para más adelante)

Decidiste publicar con la clave de Prism, como hace ElyPrismLauncher. Lo que eso implica: al iniciar sesión, la página de
Microsoft dice "Prism Launcher"; Prism o Microsoft podrían bloquear esa clave algún día; y si más adelante pasás a una clave
propia, los usuarios tienen que volver a iniciar sesión una vez. Si algún día conseguís una cuenta de Azure, estos son los pasos:

1. Entrá a https://portal.azure.com con tu cuenta de Microsoft.
2. Buscá **Microsoft Entra ID** › **App registrations** › **New registration**.
3. Nombre: `ZERO Launcher`. Tipo de cuentas: **Personal Microsoft accounts only**. Registrar.
4. Copiá el **Application (client) ID** (es un código tipo `xxxxxxxx-xxxx-...`). Esa es tu clave. **No la pegues en ningún
   chat ni la subas a GitHub**: me la pasás solo cuando la necesitemos para compilar, como variable de entorno.
5. En la app: **Authentication** › activá **Allow public client flows** = Yes, y agregá la plataforma
   **Mobile and desktop applications** con estas dos direcciones de retorno (redirect URIs), exactas:
   `zerolauncher://oauth/microsoft` (la usa el ZERO instalado) y `http://localhost` (la usa el portable).
   Sin la primera, "Iniciar sesión" con el navegador falla con `invalid_request ... redirect_uri` (te pasó probando:
   hoy, con la clave de Prism, ZERO usa siempre la segunda).
6. **Pedí permiso a Mojang** para que tu app pueda usar la API de Minecraft: formulario https://aka.ms/mce-reviewappid
   con el client ID del punto 4. Sin esta aprobación el inicio de sesión falla aunque la clave sea correcta.

## Paso 3 — Tu propia clave de CurseForge (✅ HECHO)

Ya la tenés: está guardada en `C:\Users\64027\.zero\curseforge-api-key`, fuera del repo. No borres ese archivo; si
algún día la cambiás en la consola de CurseForge, reemplazá el contenido de ese archivo. Lo que se hizo para conseguirla:


1. Entrá a https://console.curseforge.com e iniciá sesión.
2. Pedí una **API key** (llenan un formulario con el nombre del proyecto y para qué la usás: "launcher de Minecraft
   que instala mods y modpacks de CurseForge").
3. Cuando te la den, guardala igual que la de Microsoft (nunca en GitHub ni en chats).

Mientras no la tengas, se puede publicar una versión sin CurseForge: esa parte se oculta sola si la clave está vacía.

## Paso 3b — Ely.by (para las cuentas Ely.by)

Hecho (2026-11-13): la app de ZERO en Ely.by es `zerocraft-launcher` (tipo Website, sin secret). Ely.by ya funciona con email y
contraseña sin registro; el ID agrega el inicio de sesión con código.

La app se administra en https://account.ely.by/dev/applications

## Paso 4 — Decisiones

- **Número de versión**: `0.1.0` hoy. Sugerencia: el primer lanzamiento público como `1.0.0`, o `0.9.0` si querés
  marcarlo como "beta".
- **¿Firmar el programa?** Sin firma, Windows muestra "Windows protegió tu PC" la primera vez (se pasa con *Más información*
  › *Ejecutar de todas formas*). La firma de código cuesta dinero al año; para empezar no hace falta.
- **Servidor de "meta"** (la lista de versiones de Minecraft y loaders): por ahora seguimos usando el de Prism, que es público.

## Paso 5 — Probar el paquete (cuando llegues)

1. Abrí la carpeta `dist` del proyecto y descomprimí `ZEROLauncher-0.1.0-Windows-Portable.zip` (96 MB, ya armado) en el
   Escritorio. Es un paquete de **prueba**: usa las claves de Prism, así que sirve para probar pero **no se publica**.
   Para rearmarlo después de un cambio: en Git Bash, `scripts/zero/package.sh` (tarda ~1 minuto).
2. Abrí `zerolauncher.exe` desde esa carpeta. Tiene que abrir igual que la versión de desarrollo.
3. Probá lo de la lista "Owner QA" de `HANDOFF.md` (temas, actualización segura, Boost performance, Export/Import).
4. Lo que no ande, me lo decís y lo arreglo.

Los datos de la versión portable quedan dentro de esa carpeta (no toca tu Prism ni tu ZERO de desarrollo).

**El instalador** (`dist\ZEROLauncher-0.1.0-Windows-Setup.exe`, ya armado) se prueba aparte y con cuidado: instalar ZERO
hace que los links `curseforge://` y `modrinth://` de las webs ("Install") abran ZERO en vez de tu Prism (igual que hace
Prism al instalarse). Si después desinstalás ZERO, esos links se borran y Prism los recupera cuando lo reinstales o lo
abras con su opción de "registrar". Al abrir el ZERO instalado por primera vez te pregunta si querés **copiar** tus
instancias de Prism: decí "No" si solo estás probando. Se desinstala desde *Configuración › Aplicaciones*.

## Paso 6 — Publicar (cuando todo esté probado y tengas la clave de CurseForge)

Esto lo hago yo cuando me digas "publicá": compilo con `--release` usando tu clave de CurseForge (y la de Microsoft de Prism), armo zip + instalador + sumas
SHA-256, y creo el **Release** en tu repo con `gh` (solo con tu OK explícito, porque es público).
Antes conviene "poner lindo" el repo:
- README nuevo de ZERO (capturas, qué es, descarga, créditos a Prism y licencia GPL-3.0).
- Revisar `.github/workflows`: hoy son los de Prism (build, publish, codeql…). En un fork vienen apagados, pero antes de
  publicar hay que decidir cuáles quedan para que no fallen ni publiquen algo por su cuenta.
- Commits: todo lo de estas sesiones está **sin commitear**; los commits los hacés vos (o me pedís que los haga).
