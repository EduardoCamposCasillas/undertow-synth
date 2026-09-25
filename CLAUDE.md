# CLAUDE.md — Sintetizador Wavetable (JUCE / C++ moderno)

## Objetivo del proyecto
Construir un sintetizador wavetable polifónico de calidad profesional (referencia de nivel: Serum 2 y Vital),
con nombre, diseño y código propios. Se usa principalmente en FL Studio sobre Windows.

El dueño del proyecto está APRENDIENDO programación de audio y DSP mientras lo construimos.
El objetivo es doble: un plugin de nivel profesional y que él entienda cada parte.

## Modo de trabajo (muy importante)
- Trabajar SIEMPRE por fases pequeñas. Una fase a la vez.
- Antes de implementar un concepto de DSP nuevo, explicarlo en español en 1–3 párrafos:
  qué problema resuelve, la idea matemática básica y cómo se traduce a código.
- Al terminar cada fase: compilar, confirmar que no hay warnings y explicar cómo probarla en FL Studio
  (qué tocar, qué debería escucharse y qué artefactos buscar).
- NO avanzar a la siguiente fase hasta que yo confirme que la probé en FL Studio.
- Si hay varias formas de resolver algo, explicar brevemente las opciones y por qué se elige una.
- Comentarios en el código en español, educativos pero concisos (explicar el "por qué", no el "qué").
- Al terminar cada fase, actualizar la sección "Estado actual" de este archivo.
- Nunca copiar código de Vital, Surge u otros proyectos GPL. Se pueden estudiar sus ideas, no su código.

## Aprendizaje de diseño sonoro (igual de importante que el código)
Además de programar, quiero aprender a DISEÑAR SONIDOS. Al final del proyecto debo poder crear
mis propios sonidos desde cero, no solo usar presets. Por eso, al terminar cada fase:

1. **Qué hace cada control al sonido.** Por cada parámetro nuevo, explicar en lenguaje musical
   (no solo técnico) qué se escucha al moverlo: brillo, cuerpo, ataque, ancho, movimiento, agresividad.
   Relacionarlo con lo que se ve en un analizador de espectro y en un osciloscopio.
2. **Ejercicio de escucha guiado.** Pasos concretos para hacer en FL Studio: qué nota tocar, qué perilla
   mover, de qué valor a qué valor, qué debo notar. Si ayuda, sugerir un proyecto de FL sencillo
   (un patrón en el Piano Roll) para escuchar el cambio en contexto.
3. **Receta de sonido.** Con SOLO lo que existe hasta esa fase, una receta paso a paso para crear
   1 o 2 sonidos reales (ej. un sub bass, un pluck, un pad), explicando por qué cada ajuste está ahí.
4. **Reto sin receta.** Describir un sonido objetivo (o una referencia de un género) y dejar que yo
   lo intente primero; después darme pistas, no la solución completa de golpe.
5. **Vocabulario.** Los términos nuevos de la fase (ej. "armónicos", "detune", "resonancia", "transitorio")
   con una definición corta y un ejemplo de dónde se escucha en música real.

Mantener un archivo `APRENDIZAJE.md` en la raíz del proyecto, actualizado al final de cada fase, con:
conceptos aprendidos, recetas de sonidos, vocabulario y los retos completados. Es mi cuaderno de
diseño sonoro; debe servirme como referencia aunque no esté abierto el código.

Los presets de fábrica (Fase 10) deben incluir notas que expliquen cómo se construyó cada sonido,
para que funcionen también como material de estudio.

## Stack técnico
- C++20, compilador MSVC (Visual Studio 2026 o 2022, workload "Desarrollo para el escritorio con C++").
- JUCE 9 (tag 9.0.2) integrado con CMake mediante FetchContent, fijado a un tag estable (nunca a una rama).
- CMake >= 3.22.
- Formatos: VST3 + Standalone (Standalone para probar rápido sin abrir FL).
- Validación: pluginval antes de dar por terminada cualquier fase que toque parámetros, estado o procesamiento.

## Comandos de compilación
```
cmake -B build -G "Visual Studio 17 2022" -A x64    # VS Community 2022 detectado en esta máquina
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure   # tests de DSP/voces (UndertowTests, sin JUCE)
```
- CMake está en `C:\Program Files\CMake\bin` y no está en el PATH: usar la ruta completa o agregarlo.
- El VST3 queda en `build/UndertowSynth_artefacts/Release/VST3/Undertow Synth.vst3`.
- NO copiar automáticamente a `C:\Program Files\Common Files\VST3` (requiere administrador).
  En FL Studio se agrega la carpeta de artefactos como ruta de búsqueda en el Plugin Manager.

## Reglas del hilo de audio (NO negociables)
Dentro de `processBlock` y de todo lo que llame:
- Nada de reservar o liberar memoria (`new`, `delete`, `std::vector::push_back`, `resize`, `std::string`, etc.).
- Nada de locks, mutex, esperas, I/O de archivos, logging ni llamadas al sistema.
- Todo buffer se reserva en `prepareToPlay`.
- Comunicación GUI ↔ audio solo con atómicos, `AudioProcessorValueTreeState` o colas lock-free.
- Todo parámetro que afecte al sonido se suaviza (`SmoothedValue`) para evitar clics y zipper noise.
- Denormals desactivados (`juce::ScopedNoDenormals`) al inicio de `processBlock`.
- El sonido debe ser correcto a 44.1, 48, 88.2 y 96 kHz y con cualquier tamaño de buffer.

## Estilo de código
- C++ moderno: RAII, `std::unique_ptr`, `std::array`, `constexpr`, `noexcept` en código de audio.
- Nada de `new`/`delete` manuales ni punteros crudos con propiedad.
- DSP separado de JUCE cuando sea razonable (clases puras en `Source/dsp/`), para poder probarlas aisladas.
- Nombres claros en inglés para el código; comentarios en español.
- Tests unitarios para piezas de DSP críticas (osciladores, filtros, envolventes) cuando aporten valor.

## Estructura sugerida
```
Source/
  PluginProcessor.h/.cpp   # parámetros, estado, processBlock
  PluginEditor.h/.cpp      # GUI
  dsp/                     # osciladores, filtros, envolventes, LFOs (sin dependencias de GUI)
  synth/                   # voces, gestión de polifonía, matriz de modulación
  gui/                     # componentes visuales propios
```

## Hoja de ruta
- [x] Fase 1 — Proyecto base: CMake + JUCE, compila VST3 y Standalone, carga en FL, onda senoidal con MIDI.
- [x] Fase 2 — Polifonía y envolvente ADSR (sin clics, voice stealing correcto).
- [x] Fase 3 — Oscilador wavetable sin aliasing (mipmaps por octava, interpolación, morphing de posición).
- [x] Fase 4 — Filtros ZDF/TPT: LP/HP/BP 12 y 24 dB, resonancia, drive, key tracking.
- [x] Fase 5 — Modulación: 2 envolventes extra, LFOs sincronizados al tempo, matriz de modulación.
- [ ] Fase 6 — Segundo oscilador, sub, ruido, unison (hasta 16 voces) con detune y ancho estéreo.
- [ ] Fase 7 — FM y warp de osciladores: FM/PM entre osciladores (OSC B → OSC A, ruido → OSC, sub → OSC),
      ring mod, y modos de warp (bend, sync, PWM, mirror, quantize/bitcrush). Todo modulable desde la matriz
      y con control de aliasing (oversampling o técnicas band-limited donde haga falta).
- [ ] Fase 8 — Efectos internos: distorsión (con oversampling), chorus, delay, reverb.
- [ ] Fase 9 — GUI profesional: visualización del wavetable (mostrando el warp aplicado), arrastrar para modular, escalable.
- [ ] Fase 10 — Presets: guardado/carga, navegador, librería inicial de sonidos.
- [ ] Fase 11 — Pulido: optimización de CPU (SIMD), pluginval estricto, comparación contra referencias.

## Estado actual
Fase 1 COMPLETADA (2026-09-24): CMake + JUCE 9.0.2, VST3 + Standalone, seno monofónico (prioridad a la última
nota) con MIDI sample-accurate y rampa anti-clic de 5 ms. Release con 0 warnings (/W4, MSVC 19.44, VS 2022 17.14),
pluginval strictness 10: SUCCESS (`tools/pluginval/pluginval.exe`). Probado por el usuario en FL Studio 2025: suena bien.
Nota FL: la ruta de búsqueda de los artefactos debe estar marcada como tipo VST3 en Manage plugins.

Fase 2 COMPLETADA (2026-09-24). Probada por el usuario en FL Studio: suena bien.
- `dsp/AdsrEnvelope.h`: ADSR exponencial (tiempos exactos gracias al overshoot), attack desde el nivel actual,
  sustain suavizado y `quickRelease` lineal de 5 ms para el robo de voces.
- `synth/Voice.h`, `synth/VoiceManager.h/.cpp`: pool de 32 ranuras y polifonía de 1 a 16 (parámetro).
  El robo no produce clics: fade de 5 ms mientras la nota nueva suena en otra ranura.
  Prioridad para robar: nota soltada → sostenida por el pedal → pulsada más antigua.
  Repetir la misma nota reutiliza su voz. Pedal de sustain CC64, CC120 (fade) y CC123 (release).
- Parámetros APVTS (IDs en `Parameters.h`, NO cambiarlos): attack, decay, sustain, release, voices, velocity, master.
  El estado se guarda y carga como XML.
- GUI funcional: 7 perillas y contador de voces sonando.
- Tests: `tests/DspTests.cpp` (ejecutable `UndertowTests`, sin JUCE): todos pasan.
- Release con 0 warnings; pluginval strictness 10: SUCCESS.
- Tests de afinación: las 88 teclas a 4 sample rates, peor desviación 0.0003 cents.

Fase 3 COMPLETADA (2026-09-24). Probada por el usuario en FL Studio.
- `dsp/Fft.h`: FFT radix-2 propia (solo fuera del hilo de audio). `dsp/Pitch.h`: midiNoteToHz (SineOscillator eliminado).
- `dsp/Wavetable.h/.cpp`: tabla inmutable construida desde espectros (`fromSpectra`) o ciclos muestreados
  (`fromWaveforms`). 11 mipmaps por octava (1024 → 1 armónicos); tamaño por nivel = max(2048, 16·armónicos)
  → 16384/8192/4096/2048…; normalización de pico por frame (escala del nivel 0 para todos los niveles).
- `dsp/WavetableOscillator.h`: interpolación cúbica Catmull-Rom, morph lineal entre frames, Position suavizada
  (10 ms), fundido de 5 ms al cambiar de tabla, `reset()` al empezar nota desde silencio. Límite de armónicos:
  sr − 20 kHz a 44.1/48 kHz (el reflejo cae > 20 kHz), Nyquist a 88.2/96 kHz.
- `synth/WavetableBank`: 5 tablas de fábrica generadas por código (Basic Shapes 4 frames; Pulse Width, Harmonic
  Build, Hard Sync, Vowels de 64 frames). ~47 MB, ~0.4 s; compartido entre instancias (`SharedResourcePointer`).
  El orden de `names` es parte del estado guardado: solo añadir al final.
- Parámetros nuevos: oscAWavetable (choice), oscAPosition (0..1), versionHint 2. Por defecto = seno (sonido de Fase 2).
- GUI: grupo "Oscilador A" con selector, perilla Position y visor de la forma de onda (`gui/WavetableDisplay.h`).
- Tests: peor alias medido < 20 kHz: −87 dB (44.1/48 kHz) y −92 dB (88.2/96 kHz) con pulso 3 % y sync; sierra
  ingenua: −12 dB. Armónicos altos conservan su nivel (0.00 dB). Morph y cambio de tabla sin clics.
- Release con 0 warnings (build limpio); pluginval strictness 10: SUCCESS.
- Límite conocido: el paso de mipmap es por octava; con pitch bend/glide (Fase 5) podría oírse un leve salto de
  brillo al cruzar un límite. RESUELTO en Fase 5 (fundido entre niveles vecinos).

Fase 4 COMPLETADA (2026-09-24). Probada por el usuario en FL Studio: suena bien.
- `dsp/Filter.h`: `SvfStage` (SVF TPT/ZDF de 2 polos: LP/BP/HP a la vez) y `Filter` (drive → etapa 1 → etapa 2).
  12 dB = 1 etapa (Q Butterworth 0.707); 24 dB = 2 etapas (Q 0.541 y 1.307). La resonancia multiplica el Q de la
  etapa resonante hasta ×17 (exponencial; 12 dB: Q 12, pico +15 dB tras compensar). Compensación LP/HP: −6 dB en la
  banda de paso a resonancia máxima (tipo ladder). Band-pass normalizado a pico 0 dB. Sin auto-oscilación.
  Todo suavizado 5 ms por muestra (cutoff en octavas); tipo y pendiente se cambian con fundido (la etapa 2 siempre
  procesa). Coeficientes (tan) solo se recalculan mientras cutoff/resonancia se mueven.
  Drive: tanh con hasta +30 dB, mezclado con la señal limpia en el primer 10 % (drive 0 = lineal exacto).
  `filterMagnitude()`: respuesta teórica (la usa la GUI y la verifican los tests).
- `keyTrackedCutoff()`: referencia C5 de FL (MIDI 60). Cada voz aplica su propio key tracking.
- Voz: oscilador → filtro → envolvente. Filtro on/off con fundido de 5 ms; apagado no se procesa (= sonido Fase 3).
- Parámetros nuevos (versionHint 3): filter1On (off), filter1Type (LP/HP/BP), filter1Slope (12/24, def. 24),
  filter1Cutoff (20 Hz–20 kHz log, def. 2 kHz), filter1Resonance, filter1Drive, filter1KeyTrack (0 %).
  El orden de las opciones de Type/Slope se guarda: no cambiarlo.
- GUI: grupo "Filtro" (On, tipo, pendiente, 4 perillas) y visor de la curva (`gui/FilterResponseDisplay.h`).
- Tests: medido vs teórico ≤ 0.0001 dB (3 tipos × 2 pendientes × 3 resonancias × 4 sample rates); −3 dB exactos en
  el cutoff hasta 15 kHz; 3 octavas: −36.5 / −73 dB; estable con cutoff aleatorio por muestra y resonancia máxima;
  cambios de tipo/pendiente/on-off sin clics. CPU: 16 voces sierra + filtro 24 dB en movimiento ≈ 5.5 % de un núcleo.
- Release con 0 warnings (build limpio); pluginval strictness 10: SUCCESS.
- Límite conocido: el drive no tiene oversampling. Alias medido (sierra, 48 kHz, drive 100 %): −55 dB a 110 Hz,
  −43 dB a 440 Hz, −27 dB a 1760 Hz. Solución prevista: oversampling en Fase 7/8.

Fase 5 COMPLETADA (2026-09-25). Probada por el usuario en FL Studio: suena bien.
- `dsp/Lfo.h`: LFO bipolar (Sine, Triangle, Saw Up/Down, Square, Sample & Hold), fase en double + contador de ciclos.
  S&H = hash de (semilla, ciclo): sin estado, repetible y el mismo en todas las voces en modo Free. One Shot = 1 ciclo.
- `synth/Modulation.h`: enums y nombres de fuentes (None, Env 1 (Amp), Env 2, Env 3, LFO 1, LFO 2, Velocity, Key,
  Mod Wheel, Aftertouch) y destinos (None, Osc A Position, Osc A Pitch, Filter Cutoff, Filter Resonance, Filter
  Drive, Volume); 17 divisiones de tempo (4/4). TODOS los órdenes se guardan: solo añadir al final.
  Escalas del amount (100 %): Position/Resonance/Drive = toda la perilla; Pitch ±24 st; Cutoff 10 octavas;
  Volume ×(1 + m) limitado a 0..2. Key = (nota − 60) / 60 → Key→Cutoff 50 % = key tracking 100 % exacto.
- Voz: fuentes y matriz por muestra. Env 2/3 por voz (reset al empezar desde silencio). LFO por voz: Retrigger/One
  Shot reinician en cada nota; Free se re-alinea con el reloj común del VoiceManager al inicio de cada render
  (con Sync + host reproduciendo, la fase sale de ppqPosition). Suavizado: LFO 1 ms, velocity/mod wheel/aftertouch
  10 ms, amount 5 ms; envolventes SIN suavizar (Env→Cutoff llega en 1 ms). Cambiar la fuente/destino de una ruta:
  la vieja se apaga en 5 ms y la nueva entra desde 0 (`activeRoutes`). Primera muestra de una nota: valores
  directos (snap). Sin rutas activas la matriz no se calcula.
- La modulación se suma DESPUÉS del suavizado de las perillas: `Filter::setModulation` (cutoff en octavas,
  resonancia/drive; coeficientes recalculados por grupos solo si cambian) y
  `WavetableOscillator::setPitchModulation/setPositionModulation`. Refactor del filtro: se suavizan resonancia/drive/
  pendiente en espacio de parámetro y los k se derivan (mismo resultado medido que en Fase 4).
- Resuelto el límite de la Fase 3: fundido entre mipmaps en los últimos 1/6 de octava antes de cada límite
  (mezcla hacia el nivel más pobre: sin alias nuevo).
- Parámetros nuevos (versionHint 4; IDs en `Parameters.h`): env2/env3 Attack/Decay/Sustain/Release (def. 5 ms,
  500 ms, 0 %, 150 ms); lfo1/lfo2 Shape, Mode (def. Retrigger), Sync (def. on), Rate (0.02–40 Hz log, def. 2 Hz),
  Division (def. 1/4); mod1..mod8 Source, Destination, Amount (−100..100 %). Todo vacío por defecto = sonido Fase 4.
  MIDI: CC 1 → Mod Wheel, channel pressure → Aftertouch. Tempo/posición de `getPlayHead()` (120 BPM sin host).
- GUI: dos pestañas ("Sonido" = Fase 4; "Modulación" = LFO 1 | LFO 2 con visor y punto de fase, Env 2 | Env 3,
  matriz 2×4). Tamaño igual al de la Fase 4 (870×602): la versión en una sola ventana de 1470 px no cabía en la
  pantalla del usuario con escalado de Windows. Literales con tildes: usar `juce::String::fromUTF8` con escapes.
- Tests (14 nuevos): formas y velocidad exacta del LFO, One Shot, S&H, tempo sync, fase desde ppq, Free vs
  Retrigger, Key→Cutoff = key tracking, Mod Wheel→Pitch (+12 st y +1 st, error 0.0000 cents), vibrato ±100 cents,
  Env 2→Cutoff (pico 6400 Hz en 1.0 ms), modulación con saltos sin clics, continuidad del fundido de mipmaps.
  CPU: 16 voces + filtro 24 dB sin rutas ≈ 7–8 % de un núcleo; con 8 rutas activas ≈ 12–14 %.
- Release con 0 warnings (build limpio); pluginval strictness 10: SUCCESS (compilado en `out/pv` porque FL tenía
  el VST3 de `build/` abierto y bloqueado).
- Pendiente de ideas para más adelante: fade-in/delay del LFO, modular el amount con otra fuente (aux), pitch bend.

Siguiente: Fase 6 (segundo oscilador, sub, ruido, unison), pendiente de que el usuario la inicie.
