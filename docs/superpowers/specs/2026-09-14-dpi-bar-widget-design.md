# DPI Bar Widget (GHub-style) — Diseño

Fecha: 2026-09-14
Estado: Aprobado por el usuario (4 secciones)

## Objetivo

Reemplazar la sección de spinboxes de DPI (6 QSpinBox + QComboBox de stage activo) por un
widget personalizado tipo GHub: una barra horizontal con 6 puntos arrastrables, uno por stage,
sobre escala logarítmica del rango 50–26000.

Motivación del usuario: estandarizar los tamaños de la sección DPI y lograr un diseño más
parecido al de Logitech GHub.

## Recursos

- Ventana actual fija 720×440 → crece a **740×520**.
- Sección DPI (hoy `gridLayoutWidget_dpi` en y=282, 491×80) → nuevo widget **~700×140**.
- Fila infoDPI (parte superior) cambia de tamaño para acomodar la nueva altura.
- Botón Apply y barra inferior (battery/debug/settings) se reacomodan dentro de la nueva altura.
- Alterar únicamente: `atsx11.ui`, `atsx11.cpp`, `CMakeLists.txt`, `README.md`.
  Agregar: `dpiscale.h/cpp`, `dpibar.h/cpp`, tests.

## Componentes

### Módulo puro `dpiscale` (testable, sin Qt)

Funciones puras de mapeo barra↔DPI:

- `double dpiToPos(int dpi, int trackMin, int trackMax)`
  - Escala logarítmica: `(log(dpi) - log(50)) / (log(26000) - log(50))`.
  - Returns posición normalizada en `[0, 1]`.
- `int posToDpi(double pos, int trackMin, int trackMax, bool nearestStep)`
  - Inversa de `dpiToPos`, clamp a 50–26000.
  - Con `nearestStep=true`, redondea al múltiplo de 50 más cercano (para drag).
- `int roundToStep(int dpi)`
  - Redondeo a múltiplos de 50, clamp 50–26000.
- `bool clampOrdered(int dpi[6])`
  - Mantiene orden ascendente estricto y separación mínima de 100 DPI entre puntos
    adyacentes (50 si valores adyacentes, ver nota abajo): cada punto se encaja entre
    sus vecinos. Devuelve `true` si algo cambió.
  - Un-valued conflictivo: si el usuario quiere el mismo valor en dos stages, el
    segundo se desplaza al paso siguiente hacia arriba (50). No se permiten duplicados
    en la misma posición gráfica.

### Widget `DpiBarWidget` (`dpibar.h/cpp`, Qt Widgets)

`QWidget` que dibuja la pista, los 6 puntos y los números, y maneja interacción.

**Datos**
- `int m_values[6]` (DPI por stage, siempre ordenado, clamp 50–26000, paso 50).
- `int m_activeStage` (1-based, 1–6).
- `bool m_editing` (QLineEdit inline activo).

**API pública**
- `void setValues(const int values[6])` — aplica `clampOrdered`, redibuja; **no** emite `valueChanged`
  si los valores no cambiaron (para evitar loops al cargar desde QSettings).
- `void setActiveStage(int stage)` — clamp 1–6, emite `stageActivated` si cambió.
- `int activeStage() const`, `const int *values() const`.
- Señales: `void valueChanged(int stage, int dpi)`, `void stageActivated(int stage)`.

**Renderizado (paintEvent)**
- Pista horizontal en la banda media del widget, con padding de ~40px a cada lado para los
  números y los extremos `50`/`26000`.
- Gradiente de la pista (oscuro→claro en X) con el color base/gris de la app.
- Track: rectangulo redondeado, altura ~8px.
- "Glow"/fill del track entre el punto más a la izquierda y el activo, en color de acento
  (`#2a82da`) cuando el activo no es Stage 1 (análogo GHub: el trazo resalta hasta el punto
  activo). El punto activo es un círculo de 14px relleno acento con anillo blanco; los
  inactivos son gris `#51494a` con borde gris claro.
- Números siempre visibles **encima** de cada punto: fuente pequeña (~10px) gris claro para
  inactivos; el activo en mayor tamaño (~12px) y en color acento claro.

**Interacción (eventos)**
- `mousePressEvent`: hit-test a ≥8px de un punto → inicia drag de ese punto y lo activa
  (emite `stageActivated`).
- `mouseMoveEvent`: durante drag, convierte X a DPI con `posToDpi(nearestStep=true)`, aplica
  `clampOrdered`, actualiza `m_values`, repaint, emite `valueChanged` live.
- `mouseReleaseEvent`: finaliza drag.
- `mouseDoubleClickEvent`: sobre un punto, abre `QLineEdit` inline centrado en el punto con el
  valor actual; Enter = validar (50–26000, paso 50) y aplicar; Esc = cancelar; focusOut = aplicar.
  Al aplicar: `setValues` + `valueChanged` + cierra editor.
- `wheelEvent`: sobre un punto → ±50 por notch en ese punto; sobre la pista (sin punto en target,
  dentro del rect de track) → ±50 en la stage activa. Aplica `clampOrdered` + `valueChanged`.
- `keyPressEvent`: con un punto activo — `w`/`Up`/`Right` = +50; `s`/`Down`/`Left` = −50;
  con Shift = ±250. `clampOrdered` + `valueChanged`. No captura teclas cuando el QLineEdit tiene foco.

**Los QSpinBox y QComboBox de DPI se eliminan del `.ui`.** El widget se inserta en su lugar
dentro del QMainWindow, manteniendo el estilo de la app (Fusion dark theme, palettes heredadas).

## Integración en `atsx11.cpp`

- `loadSettings()`: lee `dpiValues` (QVariantList, default `[800,1600,2400,3200,5000,22000]`)
  y `activeDpiStage` (default 1) → `ui->dpiBar->setValues(...)` + `setActiveStage(...)`.
  Los `spn_dpi*` ya no existen.
- `saveSettings()`: escribe `dpiValues` desde `ui->dpiBar->values()` y `activeDpiStage`
  desde `ui->dpiBar->activeStage()` (solo si cambió; ver "Persistencia live").
- `reloadSettingsUi()` (perfiles): mismo patrón que `loadSettings`, escribe al widget.
- `updateInfoLabels()`: el label `lbl_activeDpiValue` se actualiza desde `activeStage()`
  y `values()[activeStage-1]`.
- Conexiones:
  - `dpiBar->stageActivated` → `updateInfoLabels` + `saveSettings`
  - `dpiBar->valueChanged` → guardar QSettings en debounce simple (bandera `m_dpiDirty`,
    flush en `closeEvent` y en Apply; o guardar directo — decisión de implementación, ambos
    válidos; el diseño prefiere **guardar directo + flush**).
- `on_btn_apply_clicked`: usa `ui->dpiBar->values()` y `ui->dpiBar->activeStage()`; el resto del
  flujo (applySettingsFromUser) no cambia.

## Persistencia y consistencia

- Claves QSettings sin cambios: `dpiValues` (QVariantList), `activeDpiStage` (int 1-based).
- Compatibilidad total con configs existentes.
- Defensa en profundidad: widget clamp 50–26000/paso 50 (fuente UI), `convertDpiToBytes`
  clamp interno, `hook.cpp` validación de rango — se mantiene.
- `applySettingsFromUser`, `hook.cpp`, `dpi.cpp` **no se modifican**.

## Testing

1. **CTest `dpiscale_tests`** (target nuevo en CMakeLists, patrón `dpi_encoding_tests`):
   - `dpiToPos`: pos(50)=0, pos(26000)=1, monotónica creciente en muestras; rangos extremo
     y puntos los defaults distribuidos en [0,1].
   - `posToDpi`: round-trip `posToDpi(dpiToPos(dpi))` ≈ dpi (dentro de ±50); clamp en 0→50
     y 1→26000; `nearestStep` redondea a pasos de 50.
   - `roundToStep`: 999→1000, 12551→12550, 50→50, 26000→26000, 49→50, 26010→26000.
   - `clampOrdered`: pares en orden correcto; duplicados resueltos; valores <50 o >26000
     clamp; límites exactos.
2. Regresión: `dpi_encoding_tests` existentes siguen pasando (no se tocan).
3. Manual en hardware (mouse real): drag, doble-click, rueda, cambiar stage activo, Apply →
   sensibilidad del mouse cambia; persistencia tras reiniciar la app.

## Plan de implementación (próximo paso)

Resultado de brainstorming → invocar writing-plans. Tareas esperadas:
1. `dpiscale.h/cpp` + tests + CMake (TDD).
2. `dpibar.h/cpp` widget (render + interacción).
3. Integración `atsx11.ui` + `atsx11.cpp` (load/save/reload/apply/updateInfoLabels).
4. Build clean + README (línea DPI sigue `[x]`).

## Fuera de alcance (YAGNI)

- Edición por perfil (Gaming/Office/Esports) con DPI distintos por perfil.
- Labels RGB por stage.
- Presets de DPI por aplicación/ventana.
- Middle-click para borrar/add stage (el hardware fija 6 etapas).