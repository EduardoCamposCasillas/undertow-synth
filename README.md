# Undertow Synth

Sintetizador **wavetable polifónico** en formato VST3 y Standalone para Windows, escrito en C++20 con JUCE.
Es un proyecto de aprendizaje de programación de audio y diseño sonoro, construido por fases, con la meta de
llegar al nivel de sintes como Serum o Vital.

![Interfaz de Undertow Synth](docs/screenshot.png)

> **Estado:** en desarrollo (fases 1–8 de 11 completadas). Ya suena y es
> usable en un DAW, pero todavía no tiene presets. Ver la [hoja de ruta](#hoja-de-ruta).

## Qué tiene hoy

- **Dos osciladores wavetable sin aliasing (A y B):** mipmaps por octava, interpolación cúbica y morphing continuo
  entre frames. Tiene 5 tablas de fábrica: *Basic Shapes*, *Pulse Width*, *Harmonic Build*, *Hard Sync* y *Vowels*.
- **Unison de hasta 16 copias por oscilador,** con detune, ancho estéreo y paneo. El volumen se mantiene al cambiar
  el número de copias y ninguna copia produce aliasing.
- **Warp, FM y ring mod en cada oscilador:** Sync, Bend + / −, PWM, Mirror, Quantize y Bitcrush; FM (en realidad PM,
  como el DX7) y ring mod desde el otro oscilador, el sub o el ruido. Con control de aliasing: mipmap según la velocidad
  real de lectura, polyBLEP/polyBLAMP de 4 muestras y oversampling ×2 con decimador halfband.
- **Sub-oscilador** (seno, triángulo, sierra o cuadrada, de 0 a −3 octavas) y **ruido** con control de color.
- **Filtro ZDF/TPT:** Low Pass, High Pass y Band Pass de 12 o 24 dB/octava, con resonancia, drive y key tracking.
  Incluye un visor de la curva de respuesta.
- **Modulación:** 2 envolventes extra (Env 2 y Env 3), 2 LFOs (6 formas, sincronizables al tempo, modos Free,
  Retrigger y One Shot) y una matriz de 8 rutas. Fuentes: envolventes, LFOs, velocity, nota, rueda de modulación y
  aftertouch. Destinos: posición, tono, nivel, detune, warp y FM/RM de cada oscilador, tono global, nivel del sub y
  del ruido, cutoff, resonancia, drive y volumen.
- **Efectos globales:** distorsión (Soft Clip, Hard Clip, Tube y Fold, con oversampling ×4 y ADAA), chorus estéreo,
  delay estéreo o ping-pong (libre o sincronizado al tempo) y reverb algorítmica (red de 8 retardos realimentados).
- **Polifonía de 1 a 16 voces** con robo de voces sin clics, pedal de sustain (CC64) y CC120/123.
- **Envolvente ADSR** exponencial y sensibilidad a la velocity.
- Correcto a 44.1, 48, 88.2 y 96 kHz y con cualquier tamaño de buffer. Validado con
  [pluginval](https://github.com/Tracktion/pluginval) en strictness 10.

## Requisitos

- Windows 10/11 de 64 bits.
- [Visual Studio 2022](https://visualstudio.microsoft.com/es/vs/community/) (Community sirve), con el workload
  **"Desarrollo para el escritorio con C++"**.
- [CMake](https://cmake.org/download/) 3.22 o superior.
- Git. JUCE 9.0.2 se descarga solo durante la configuración (no hace falta instalarlo).

No hay binarios publicados todavía: hay que compilarlo (son 3 comandos).

## Compilar

```bash
git clone https://github.com/EduardoCamposCasillas/undertow-synth.git
cd undertow-synth
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

La primera configuración tarda unos minutos porque descarga JUCE. Al terminar tendrás:

| Formato | Ruta |
|---|---|
| VST3 | `build/UndertowSynth_artefacts/Release/VST3/Undertow Synth.vst3` |
| Standalone | `build/UndertowSynth_artefacts/Release/Standalone/Undertow Synth.exe` |

Si `cmake` no se reconoce, usa la ruta completa (`"C:\Program Files\CMake\bin\cmake.exe"`) o agrégalo al PATH.

### Tests

```bash
ctest --test-dir build -C Release --output-on-failure
```

Los tests (`tests/DspTests.cpp`) miden afinación, aliasing, respuesta del filtro, ausencia de clics y coste de CPU.

## Usarlo

### Standalone (la forma más rápida de probarlo)

1. Abre `Undertow Synth.exe`.
2. En **Options → Audio/MIDI Settings** elige tu tarjeta de sonido y activa tu teclado MIDI.
3. Toca. Sin teclado MIDI, lo más cómodo es usarlo dentro de un DAW.

### En FL Studio

1. Copia la carpeta `Undertow Synth.vst3` a `C:\Program Files\Common Files\VST3` (requiere administrador).
   También puedes agregar `build/UndertowSynth_artefacts/Release/VST3` como carpeta de búsqueda.
2. **Options → Manage plugins**. Si agregaste una carpeta propia, márcala como tipo **VST3**: si queda como VST2,
   FL no encuentra el plugin. Pulsa **Find installed plugins**.
3. En el Channel Rack: **+ → Undertow Synth**.

En otros DAWs (Ableton, Reaper, Bitwig…) funciona igual: hay que reescanear la carpeta VST3.

### Controles

**Pestaña Osciladores: Oscilador A y Oscilador B** (B viene apagado)
| Control | Qué hace |
|---|---|
| On | Enciende o apaga el oscilador (con un fundido corto: sin clics). |
| Wavetable | Elige la familia de timbres. Cambiarla con notas sonando no produce clics. |
| Position | Recorre los frames de la tabla. En *Basic Shapes*: 0 % seno, 33 % triángulo, 67 % sierra y 100 % cuadrada. |
| Octave / Semi / Fine | Afinación: ±4 octavas, ±12 semitonos y ±100 cents. |
| Level / Pan | Volumen del oscilador y posición en el estéreo. |
| Unison | Cuántas copias de la onda suenan a la vez (1 a 16). |
| Detune | Cuánto se desafinan las copias entre sí (hasta ±1 semitono). Poco = coro suave; mucho = supersaw. |
| Width | Cuánto se abren las copias hacia izquierda y derecha. |

**Sub y Ruido**
| Control | Qué hace |
|---|---|
| Sub: forma, Octave, Level | Un oscilador simple por debajo de la nota (mono, siempre al centro) para dar peso en los graves. |
| Ruido: Level, Color | Ruido para aire, ataques o texturas. Color 0 % = grave y oscuro, 50 % = blanco, 100 % = solo agudos. |

**Pestaña Warp y FM** (una columna por oscilador; el dibujo muestra la onda original en gris y la deformada en naranja)
| Control | Qué hace |
|---|---|
| Warp | Deforma la lectura de la onda. **Sync:** el ciclo se reinicia con la nota, con un pico de timbre que sube con el Amount. **Bend + / −:** acelera los bordes o el centro del ciclo. **PWM:** comprime la onda en una parte del ciclo. **Mirror:** ida y vuelta. **Quantize:** escalones en el tiempo. **Bitcrush:** escalones en la amplitud. |
| Warp Amount | Cuánto se deforma. Con 0 % la onda es exactamente la original. |
| FM / RM | **FM:** el otro oscilador, el sub o el ruido mueven la fase de este: brillo, metal, campanas. **RM:** se multiplican: suma y diferencia de frecuencias (robótico). El modulador funciona aunque esté apagado. |
| FM / RM Amount | Profundidad. FM: índice de hasta 4π (curva: la mitad baja es la zona sutil). RM: al 50 % es AM, al 100 % ring mod puro. |

Cambiar un modo con una nota sonando hace un fundido de ~6 ms (sin clics). Con algún warp o FM/RM activo el
oscilador trabaja a doble frecuencia de muestreo y gasta más CPU.

**Pestaña Filtro y Amp: filtro**
| Control | Qué hace |
|---|---|
| On | Enciende el filtro. Viene apagado por defecto. |
| Tipo | Low Pass (oscurece), High Pass (adelgaza) o Band Pass (suena nasal, tipo "wah"). |
| 12 / 24 dB | La pendiente: 12 dB da un corte suave y 24 dB uno más oscuro y marcado. |
| Cutoff | Dónde empieza a actuar el filtro (de 20 Hz a 20 kHz). |
| Resonance | Crea un pico en el cutoff: da presencia y, alta, suena "ácido". |
| Drive | Satura la señal antes del filtro: más armónicos y más agresividad. |
| Key Track | Hace que el cutoff siga a la nota; al 100 %, todo el teclado tiene el mismo brillo. |

**Envolvente de amplitud y voz**
| Control | Qué hace |
|---|---|
| Attack / Decay / Sustain / Release | Forma del volumen de cada nota en el tiempo. |
| Voices | Máximo de notas simultáneas (de 1 a 16). Con 1 es monofónico, ideal para bajos y leads. |
| Velocity | Cuánto afecta la fuerza de la tecla al volumen. |
| Master | Volumen de salida. |

**Pestaña Modulación**
| Control | Qué hace |
|---|---|
| LFO 1 / LFO 2 | Forma (Sine, Triangle, Saw Up/Down, Square, Sample & Hold) y modo: *Retrigger* reinicia el ciclo en cada nota, *Free* usa un reloj común (con Sync, ligado al compás) y *One Shot* hace un solo ciclo. |
| Sync / Rate / Division | Sin Sync, la velocidad va en Hz (0.02–40 Hz). Con Sync, en figuras musicales (de 8 compases a 1/32, con tresillos y puntillos). |
| Env 2 / Env 3 | Envolventes ADSR que no mueven el volumen, sino lo que se les conecte. |
| Matriz (8 rutas) | Fuente → destino con un amount de −100 % a +100 %. El 100 % recorre toda la perilla del destino (Cutoff: 10 octavas; Pitch: ±24 semitonos). Doble clic en la barra = 0 %. |

**Pestaña Efectos** (orden fijo de la señal: distorsión → chorus → delay → reverb; todos apagados por defecto)
| Control | Qué hace |
|---|---|
| Distorsión: modo | **Soft Clip:** saturación suave y cálida. **Hard Clip:** recorte duro, agresivo. **Tube:** asimétrica, añade armónicos pares (más "gorda"). **Fold:** pliega la onda sobre sí misma: timbre metálico que cambia mucho con el drive. |
| Drive / Tone / Mix | Cuánto se empuja la señal hacia la curva (hasta +36 dB; Fold hasta +20 dB), un low-pass después (100 % = abierto) y la mezcla con la señal limpia. El volumen se compensa solo. |
| Chorus: Rate / Depth / Feedback / Mix | Velocidad y profundidad del vaivén, realimentación (hacia flanger) y mezcla. Ensancha el estéreo. |
| Delay: Sync / Time / Division | Tiempo del eco: libre (1 ms – 2 s) o en figuras musicales (1 compás a 1/32, con tresillos y puntillos). |
| Delay: Ping-Pong / Feedback / Tone / Mix | Ecos que rebotan entre L y R; cuántas repeticiones; cada repetición más oscura; mezcla. |
| Reverb: Size / Decay / Damping / Pre-Delay / Mix | Tamaño de la sala, tiempo de caída (RT60, de 0.2 a 30 s), cuánto se apagan los agudos, hueco antes de la reverb y mezcla. |

Todos los parámetros se pueden automatizar desde el DAW (en FL: clic derecho → *Create automation clip*).

### Primer sonido: bajo analógico

*Basic Shapes*, Position **67 %** · Voices **1** · Filtro **On**, **Low Pass 24 dB**, Cutoff **450 Hz**,
Resonance **25 %**, Drive **30 %**, Key Track **50 %** · Attack **2 ms**, Decay **300 ms**, Sustain **60 %**,
Release **60 ms**. Toca corcheas entre C3 y C4 (notación de FL).

Para darle el "squelch" de un acid: Resonance **75 %** y, en la pestaña Modulación, Env 2 (Attack 1 ms, Decay 180 ms,
Sustain 0 %) con la ruta **Env 2 → Filter Cutoff +40 %**.

Hay más recetas, ejercicios de escucha y vocabulario en **[APRENDIZAJE.md](APRENDIZAJE.md)**, el cuaderno de
diseño sonoro del proyecto. Explica qué se oye con cada control y por qué.

## Estructura del código

```
Source/
  PluginProcessor.*   parámetros, estado y processBlock
  PluginEditor.*      interfaz
  dsp/                oscilador con unison, warp y FM, wavetables, oversampling halfband, ruido, filtro, envolvente, LFO,
                      FFT y efectos: distorsión, chorus, delay y reverb (sin JUCE)
  synth/              voces, gestión de polifonía, matriz de modulación, banco de wavetables y cadena de efectos
  gui/                visores propios (forma de onda, warp, curva del filtro y LFO)
tests/                tests de DSP (ejecutable independiente, sin JUCE)
```

El DSP no depende de JUCE: se puede probar aislado. Dentro del hilo de audio no se reserva memoria ni se usan locks.

## Hoja de ruta

- [x] 1 — Proyecto base, VST3 + Standalone
- [x] 2 — Polifonía y envolvente ADSR
- [x] 3 — Oscilador wavetable sin aliasing
- [x] 4 — Filtros ZDF/TPT
- [x] 5 — Modulación: envolventes extra, LFOs y matriz de modulación
- [x] 6 — Segundo oscilador, sub, ruido y unison
- [x] 7 — FM, ring mod y modos de warp
- [x] 8 — Efectos: distorsión, chorus, delay y reverb
- [ ] 9 — Interfaz profesional
- [ ] 10 — Presets y librería de sonidos
- [ ] 11 — Optimización y pulido

**Límites conocidos:**
- El drive del filtro no tiene oversampling (va por voz: sobremuestrear 16 voces sería caro). Con drive alto en notas muy
  agudas puede aparecer algo de aliasing; para una saturación agresiva y limpia, usa la distorsión de la pestaña
  Efectos, que sí tiene oversampling ×4. Pendiente de revisar en la fase 11 (ADAA en el drive del filtro).
- Aliasing medido de la distorsión (seno de 0 dBFS): hasta 1.2 kHz, −67 a −92 dB; en C8 con drive 100 %, unos −55 dB
  (Soft Clip ingenuo sin oversampling: −17 dB). Añade ~1 ms de retardo mientras está encendida.
- Los efectos no son destinos de la matriz de modulación (la matriz es por voz y los efectos son globales).
- El unison es caro con todo al máximo (16 notas con los dos osciladores a 16 copias ≈ 65–80 % de un núcleo). Un
  supersaw normal (8 notas, 7 copias) cuesta ≈ 9 %. Con warp o FM el oscilador cuesta de 2 a 4 veces más (8 notas con
  Sync ≈ 9 %, FM de 2 osciladores ≈ 14 %). La optimización con SIMD llega en la fase 11.
- Aliasing medido de los warps (peor caso de C4 a C7): entre −72 y −92 dB, salvo Bend − al 100 % (−62 dB) y Bitcrush de
  una sierra (−53 dB). En C8 con amounts extremos la lectura supera los 30 kHz y el alias sube (−53 a −79 dB).

## Licencia

[GNU AGPLv3](LICENSE). Undertow Synth usa [JUCE](https://juce.com), que se distribuye bajo AGPLv3 o bajo una
licencia comercial.
