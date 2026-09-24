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
- [ ] Fase 3 — Oscilador wavetable sin aliasing (mipmaps por octava, interpolación, morphing de posición).
- [ ] Fase 4 — Filtros ZDF/TPT: LP/HP/BP 12 y 24 dB, resonancia, drive, key tracking.
- [ ] Fase 5 — Modulación: 2 envolventes extra, LFOs sincronizados al tempo, matriz de modulación.
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

Siguiente: Fase 3 (oscilador wavetable), pendiente de que el usuario la inicie.
