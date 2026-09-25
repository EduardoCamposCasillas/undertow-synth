# Cuaderno de diseño sonoro — Undertow Synth

> **Nombres de notas:** en este cuaderno las notas se nombran **como en el Piano Roll de FL Studio**.
> FL llama **C5** al Do central (nota MIDI 60, 261.6 Hz) y **A5** al La de 440 Hz (nota MIDI 69).
> Muchos libros y otros DAWs (Ableton, Logic) usan la notación científica, con una octava menos:
> ahí el Do central es C4 y el La de 440 Hz es A4. Es la misma nota con otro nombre.

---

## Fase 1 — La onda senoidal

### Conceptos aprendidos
- **Un plugin son dos objetos.** El `AudioProcessor` hace el sonido en el hilo de audio.
  El `AudioProcessorEditor` es la ventana y puede cerrarse sin que el sonido se detenga.
- **`processBlock` tiene un plazo.** FL pide bloques de N muestras y hay que entregarlos a tiempo.
  Por eso ahí dentro no se reserva memoria ni se usan locks.
- **La frecuencia de una nota MIDI** es `f = 440 · 2^((nota − 69) / 12)`.
  Subir 12 semitonos duplica la frecuencia.
- **Acumulador de fase.** En cada muestra la fase avanza `f / sampleRate` ciclos, y la salida es
  `sin(2π · fase)`. El tono sale correcto a cualquier sample rate.
- **Clics.** Si la onda empieza o se corta de golpe a mitad de ciclo, la forma de onda da un salto
  instantáneo. Ese salto contiene muchísimos armónicos agudos y se oye como un "tic".
  Una rampa de 5 ms en la amplitud lo evita.

### Qué hace cada "control" de esta fase
Todavía no hay perillas. Los controles son la **nota** y la **velocity**.
- **Nota.** Cambia la altura. Por debajo de ~C3 de FL (65 Hz) el seno se *siente* más de lo que se *oye*.
  En altavoces de portátil prácticamente desaparece, porque no tiene armónicos que "delaten" la nota.
- **Velocity.** Solo cambia el volumen; el timbre no cambia. En un seno puro no existe
  "más fuerte = más brillante". Eso llegará con filtros y modulación.
- **En el analizador de espectro** se ve **una sola línea**: la fundamental. No hay armónicos.
- **En el osciloscopio** se ve una curva suave y perfectamente redonda.

### Ejercicio de escucha guiado
1. En FL, pon Undertow Synth en el Channel Rack. En su canal del Mixer inserta **Fruity Parametric EQ 2**
   (lo usarás como analizador) y **Wave Candy** en modo osciloscopio.
2. Toca un **A5** (el La de 440 Hz). En el EQ verás un pico en 440 Hz y en Wave Candy un seno limpio.
3. Toca **A4** y luego **A6**. El pico salta a 220 Hz y a 880 Hz: cada octava duplica la frecuencia.
   En el osciloscopio caben el doble (o la mitad) de ciclos.
4. Baja nota a nota desde **C4** (131 Hz) hasta **C2** (33 Hz) y escucha **dónde "se pierde"** en tus altavoces
   y dónde en audífonos. Esa diferencia es el problema clásico del sub bass.
5. Toca notas cortas y staccato con volumen alto. Busca clics: **no debería haber ninguno**.
6. Toca legato: pulsa C4, sin soltarla pulsa E4 y luego suelta C4. E4 debe seguir sonando sin cortes
   (prioridad a la última nota).

### Recetas
**Sub bass limpio (estilo trap / future bass)**
1. Piano Roll: notas largas entre **C2 y G2** (33–49 Hz) que sigan la raíz de los acordes.
2. Velocity al 100 %. El seno no cambia de color con la velocity, así que solo sirve como volumen.
3. Pon el sub en mono (el plugin ya da la misma señal en L y R). Así funciona en clubes y en mono.
4. *Por qué funciona:* un seno sin armónicos llena los graves sin ensuciar los medios,
   donde viven la voz y los sintes.

**Sub "audible" en altavoces pequeños (usando FL)**
1. La misma línea de sub, con **Fruity Soft Clipper** o **Fruity Waveshaper** después del plugin.
2. Sube la ganancia de entrada poco a poco y mira el analizador: aparecen líneas nuevas
   en 2×, 3×, 5×… la fundamental. Son **armónicos**.
3. *Por qué funciona:* el oído "reconstruye" la fundamental a partir de sus armónicos
   (fundamental ausente). Ahora el bajo se oye también en el móvil.

### Reto sin receta
Haz un **808 corto** para un loop de trap a 140 BPM. Debe sonar debajo de un bombo de FL sin
"pelearse" con él y oírse en altavoces de portátil. Solo puedes usar este plugin y los efectos
nativos de FL. Inténtalo primero; las pistas vendrán después.

### Vocabulario
- **Frecuencia fundamental.** La frecuencia más baja de un sonido; define la nota.
  *Ejemplo:* el La del diapasón, a 440 Hz.
- **Armónicos.** Frecuencias múltiplo de la fundamental que dan el "color".
  Un seno no tiene ninguno. *Ejemplo:* por ellos un violín y una flauta tocando el mismo La suenan distintos.
- **Sub bass.** Graves por debajo de ~60 Hz, casi siempre un seno.
  *Ejemplo:* el bajo profundo de casi cualquier tema de trap o de dubstep.
- **Velocity.** La fuerza con la que se toca una nota MIDI (0–127).
- **Legato.** Notas encadenadas sin silencio entre ellas.
- **Clic.** Artefacto por una discontinuidad brusca en la forma de onda.
  *Ejemplo:* al cortar un sample sin fade.
- **Monofónico.** Una sola nota a la vez. *Ejemplo:* el Minimoog o la línea de bajo de un 303.

### Retos completados
- [ ] Reto Fase 1 — 808 corto

---

## Fase 2 — Polifonía y envolvente ADSR

### Conceptos aprendidos
- **Envolvente.** Es una curva que decide cómo cambia el volumen de una nota *en el tiempo*.
  Tiene tanta importancia como el timbre: un mismo seno puede sonar a pluck, a órgano o a pad
  cambiando solo la envolvente.
- **ADSR.** Son cuatro tramos:
  - **Attack:** tiempo que tarda el volumen en subir de 0 al máximo.
  - **Decay:** tiempo que tarda en bajar del máximo al nivel de sustain.
  - **Sustain:** *nivel* (no tiempo) que se mantiene mientras la tecla sigue pulsada.
  - **Release:** tiempo que tarda en llegar a silencio después de soltar la tecla.
- **Curvas exponenciales.** Cada tramo persigue un objetivo con `nivel = objetivo + (nivel − objetivo)·coef`.
  Es lo que hace un condensador que se carga y se descarga en un sinte analógico.
  El oído percibe el volumen en decibelios, así que una caída exponencial suena "natural".
  Una caída lineal parece quedarse colgada y cortarse al final.
- **Objetivo pasado de largo (overshoot).** El attack apunta a 1.3 en lugar de a 1. Así cruza 1 en
  un tiempo exacto y no se acerca a 1 eternamente sin llegar.
- **Polifonía.** Hay un "pool" de voces y cada voz tiene su propio oscilador y su propia envolvente.
  El resultado es la suma de todas las voces.
- **Voice stealing.** Si se supera el límite de voces, hay que quitarle la voz a alguna nota.
  Si se corta de golpe, suena un clic. Aquí la voz robada hace un fade de 5 ms mientras la nota
  nueva ya suena en otra ranura del pool.
  El orden para elegir a quién robar es: nota ya soltada → nota sostenida por el pedal → tecla pulsada más antigua.
- **Redisparo (retrigger).** Repetir una nota que aún suena reutiliza su voz, y el attack parte del
  nivel en el que estaba. No se acumulan copias y no hay saltos.
- **Pedal de sustain (CC64).** Mientras está pisado, las teclas soltadas se quedan en la fase de sustain.
- **Parámetros del host.** Ahora cada perilla es un parámetro: FL puede automatizarlos y se guardan
  con el proyecto.

### Qué hace cada control al sonido
| Control | Qué se oye | En el osciloscopio / analizador |
|---|---|---|
| **Attack** (1 ms–10 s) | Corto: la nota "golpea" y tiene transitorio. Largo: la nota "entra flotando" o hace un *swell*. | En Wave Candy, largo = la forma de onda crece como una cuña. |
| **Decay** (1 ms–10 s) | Solo se oye si el sustain está por debajo del 100 %. Corto + sustain bajo = golpe. Largo = la nota se apaga despacio. | El pico inicial baja hasta la meseta del sustain. |
| **Sustain** (0–100 %) | Cuánto "cuerpo" queda mientras mantienes la tecla. 0 % = sonido percusivo (solo suena el golpe). 100 % = órgano (el decay no hace nada). | Altura de la meseta. |
| **Release** (5 ms–10 s) | La "cola" al soltar. Corta = seco y rítmico. Larga = las notas se solapan y crean ambiente. En pasajes rápidos, un release largo "embarra". | La cola que queda después del note-off en el Piano Roll. |
| **Voices** (1–16) | Cuántas notas pueden sonar a la vez. Con 1 el sinte es monofónico: cada nota corta a la anterior. | El contador "Voces sonando" de la ventana. |
| **Velocity** (0–100 %) | Cuánto afecta la fuerza de la tecla al volumen. 0 % = todas las notas igual de fuertes (típico en sub bass). 100 % = muy expresivo. | Picos de distinta altura según la velocity. |
| **Master** (−inf a +6 dB) | Volumen de salida. | — |

Nota: el analizador de espectro sigue mostrando **una sola línea por nota**, porque el timbre sigue
siendo un seno. La envolvente cambia *cuándo* suena, no *qué* frecuencias tiene.
Es un tema de la Fase 3.

### Ejercicio de escucha guiado
1. En el Piano Roll, dibuja un acorde **C5–E5–G5** (Do central) de 1 compás y, detrás, 8 corcheas de **C5**.
   Pon el patrón en bucle a 120 BPM.
2. **Attack:** súbelo de 1 ms a 500 ms. Las corcheas pierden el golpe y parece que suenan "al revés".
   Con 1–5 ms vuelven a ser nítidas.
   Con attack de 1 ms busca clics al empezar las notas: no debería haber ninguno.
3. **Sustain y decay:** pon el sustain al 0 % y mueve el decay entre 50 ms y 1 s. Con 50 ms es un
   "tic" y con 1 s una campana apagándose. Después sube el sustain al 100 % y verás que el decay deja
   de tener efecto.
4. **Release:** con 50 ms las corcheas quedan separadas. Con 2 s se funden en una nube.
   En el acorde, suelta las teclas y escucha la cola.
5. **Voice stealing:** pon Voices en **2** y toca el acorde de 3 notas. Una nota desaparece
   con suavidad, sin clic. Pon Voices en **1**: ahora es monofónico y cada nota corta a la anterior.
6. **Velocity:** en el Piano Roll, dibuja velocities distintas en las corcheas (de 30 a 127).
   Con Velocity al 0 % todas suenan igual; con 100 %, muy diferentes.
7. **Automatización:** en FL, clic derecho en Attack → *Create automation clip*.
   Dibuja una rampa y escucha cómo el sonido evoluciona solo.

### Recetas
**Pluck (estilo marimba / pluck de house)**
1. Attack **1–3 ms**: el golpe inicial es lo que se identifica como "pulsado".
2. Decay **150–300 ms** con Sustain **0 %**: la nota se apaga sola aunque mantengas la tecla.
   Toda la forma la da el decay.
3. Release **≈ decay** (150–300 ms): la nota termina igual la sueltes pronto o tarde.
4. Velocity **70 %**: el pluck responde a la fuerza de la tecla, como un instrumento real.
5. *Por qué funciona:* los instrumentos punteados (marimba, arpa, guitarra) no tienen sustain.
   La energía se da de golpe y se disipa.

**Pad (fondo ambiental)**
1. Attack **600 ms–1.5 s**: la nota entra sin transitorio y no compite con la batería.
2. Decay **1 s**, Sustain **80 %**: una bajada leve después del ataque le da algo de vida.
3. Release **2–3 s**: los acordes se encadenan y se solapan.
4. Voices **8** o más: los acordes con release largo necesitan voces libres para que la cola
   del acorde anterior no se robe.
5. Velocity **20 %**: un pad es un fondo estable y no conviene que salte de volumen.
6. *Por qué funciona:* sin transitorio el oído lo coloca "detrás" de la mezcla.
   El seno aún suena como un órgano suave; en la Fase 3, con wavetables, ganará riqueza.

### Reto sin receta
Crea un **sonido tipo órgano de iglesia / Hammond** para un patrón de acordes staccato a 100 BPM.
Debe sonar "encendido/apagado", sin golpe, sin cola, y con todas las notas del acorde al mismo
volumen aunque dibujes velocities distintas. Después haz lo contrario: un **"swell" invertido**
que crezca durante un compás entero y se corte en seco al empezar el siguiente.
Inténtalo primero; las pistas vendrán después.

### Vocabulario
- **Envolvente (envelope).** Curva que controla cómo cambia un parámetro en el tiempo. Aquí controla el volumen.
  *Ejemplo:* la diferencia entre una nota de piano (se apaga) y una de órgano (se mantiene).
- **Transitorio.** El primer instante de un sonido, breve y con mucha energía.
  *Ejemplo:* el "clack" del palillo en un hi-hat o el golpe inicial de un pluck.
- **Pluck.** Sonido corto y percusivo sin sustain.
  *Ejemplo:* los plucks de tropical house (Kygo) o de progressive house.
- **Pad.** Sonido largo y suave que rellena el fondo armónico.
  *Ejemplo:* los colchones de acordes en temas de ambient o de synthwave.
- **Swell.** Sonido que crece poco a poco. *Ejemplo:* un acorde que "se abre" antes de un drop.
- **Polifonía.** Número de notas simultáneas. *Ejemplo:* el Prophet-5 tenía 5 voces y el Minimoog, 1.
- **Voice stealing.** Reasignar una voz ocupada a una nota nueva cuando ya no quedan voces libres.
- **Retrigger.** Volver a disparar la envolvente de una nota que aún suena.
- **Staccato / legato.** Notas cortas y separadas frente a notas unidas.
- **Automatización.** Movimiento de un parámetro grabado en el proyecto.
  *Ejemplo:* el filtro que se abre antes de un drop (lo haremos en la Fase 4).

### Retos completados
- [ ] Reto Fase 2 — órgano staccato + swell invertido

---

## Fase 3 — Oscilador wavetable

### Conceptos aprendidos
- **El timbre son los armónicos.** Dos notas iguales suenan distintas porque cada una tiene
  otros armónicos (múltiplos de la fundamental) y con otra fuerza. Las formas clásicas:
  - **Seno:** solo la fundamental.
  - **Triángulo:** solo armónicos impares (3, 5, 7…) que caen muy rápido (1/h²). Suena suave, casi un seno "con aire".
  - **Cuadrada:** solo impares, pero caen más despacio (1/h). Suena hueca, como un clarinete o un chiptune.
  - **Sierra:** todos los armónicos (1/h). Es la más brillante y "llena": la base de casi todos los leads, bajos y pads.
- **Wavetable.** Es una colección de ciclos de onda ("frames"), como los fotogramas de una película.
  El oscilador lee un ciclo con el acumulador de fase de la Fase 1: la fase (0 a 1) indica en qué punto del ciclo está.
- **Morphing (Position).** La perilla Position elige un punto entre dos frames vecinos y los mezcla.
  Si la mueves, el timbre cambia de forma continua. Es el corazón de los sintes wavetable (Serum, Vital).
- **Interpolación.** La fase casi nunca cae justo sobre una muestra guardada, así que hay que calcular el valor
  "entre" muestras. Usamos interpolación **cúbica**: 4 muestras y una curva suave. La lineal (2 muestras, una
  recta) es más barata, pero apaga los agudos y ensucia el sonido.
- **Nyquist y aliasing.** Con un sample rate *sr* solo se pueden representar frecuencias hasta *sr/2* (Nyquist).
  Un armónico que pasa de ese límite no desaparece: se **refleja** hacia abajo (en *f* aparece en *sr − f*).
  El reflejo ya no es múltiplo de la fundamental, así que suena desafinado y metálico. Además, **baja cuando la
  nota sube**. Ese es el síntoma clásico del aliasing.
- **Mipmaps (band-limiting).** De cada frame se guardan 11 versiones, cada una con la mitad de armónicos
  (1024, 512… 1). Para cada nota se usa la versión más rica que no pasa del límite. Las versiones se
  crean con una **FFT**, que convierte la onda en su lista de armónicos: se borran los que sobran y se vuelve
  a la onda.
- **Truco de los 20 kHz.** A 44.1/48 kHz dejamos que los armónicos lleguen hasta *sr − 20 kHz*.
  Su reflejo cae por encima de 20 kHz y nadie lo oye. Así las notas agudas conservan más brillo.
- **Resultado medido en los tests:** el peor aliasing está a **−87 dB** (más de 20 000 veces más débil que la nota),
  frente a **−12 dB** de una sierra "ingenua" sin mipmaps.
- **Normalización.** Todos los frames tienen el mismo *pico*, pero no el mismo *volumen percibido*:
  una cuadrada suena más fuerte que un seno con el mismo pico, porque tiene más energía.

### Qué hace cada control al sonido
| Control | Qué se oye | En el osciloscopio / analizador |
|---|---|---|
| **Wavetable** | Elige la "familia" de timbres. Cambiarla con una nota sonando hace un fundido de 5 ms, sin clic. | Cambia la forma que se ve en el visor del plugin. |
| **Position** en *Basic Shapes* | 0 % seno (redondo, sin brillo) → 33 % triángulo (suave, flauta) → 67 % sierra (brillante, zumbido) → 100 % cuadrada (hueca, 8 bits). | En el EQ aparecen primero los armónicos impares (triángulo) y luego todos (sierra); en la cuadrada desaparecen los pares. |
| **Position** en *Pulse Width* | 0 % cuadrada → 100 % pulso muy fino (3 %). Al estrecharse suena más nasal, fino y "de caña". | En Wave Candy el tramo alto se estrecha. En el EQ aparecen "huecos" regulares entre armónicos (en la cuadrada son los pares). |
| **Position** en *Harmonic Build* | Suma los armónicos uno a uno (de 1 a 64). Se parece a "abrir" un filtro, pero por escalones. | Se ve cada línea nueva del espectro aparecer a su múltiplo de la fundamental. |
| **Position** en *Hard Sync* | 0 % sierra normal → 100 % sync de 8×. Aparece un pico de brillo que "canta" y sube, como una voz metálica. | El armónico más fuerte se desplaza hacia arriba mientras la fundamental no cambia. |
| **Position** en *Vowels* | Recorre las vocales A → E → I → O → U (0, 25, 50, 75 y 100 %). Suena a "coro" o a voz robótica, sobre todo en graves y medios. | En el EQ se ven 2–3 "montañas" (formantes) que se desplazan. |

### Ejercicio de escucha guiado
1. Mismo montaje que en la Fase 1: **Fruity Parametric EQ 2** (como analizador) y **Wave Candy** en el canal
   del Mixer. Attack 5 ms, Sustain 100 %, Release 150 ms.
2. **Los 4 timbres básicos:** con *Basic Shapes*, mantén un **C4** (130.8 Hz) y lleva Position a 0, 33, 67 y 100 %.
   En el EQ cuenta las líneas: 1 (seno), impares que caen rápido (triángulo), todas (sierra), impares fuertes (cuadrada).
   En el visor del plugin verás las cuatro formas.
3. **Morph continuo:** barre Position despacio de 0 a 100 %. No debería haber saltos, clics ni "escalones" de zipper.
4. **Construir un timbre:** con *Harmonic Build*, mantén un **C4** y sube Position poco a poco. Escucha cómo cada
   armónico nuevo cambia el color: con 2–3 el sonido "se abre", con 5–8 parece un órgano y con 30 o más ya es una sierra.
5. **Aliasing (lo que NO debe pasar):** con la sierra (Position 67 %), toca una escala cromática lenta que suba de
   **C8** a **C10**. Cada nota debe sonar como un silbido limpio, sin tonos extra que **bajen** mientras la escala
   sube. Si FL está a 44.1 kHz, repite a 96 kHz (Options → Audio settings) y compara: debería sonar igual.
6. **Cambio de tabla en vivo:** mantén un acorde y cambia la tabla en el selector. El timbre cambia al instante,
   sin clic.
7. **Movimiento:** clic derecho en Position → *Create automation clip*. Dibuja una rampa de 4 compases con
   *Vowels* o *Hard Sync*. Así se oye lo que hace especial a un wavetable: **el timbre se mueve**.
8. **Artefactos a buscar:** clics al cambiar de tabla, zipper al mover Position rápido y tonos desafinados en las
   notas muy agudas. En las 2 octavas más agudas quizá notes que alguna nota es un poco más oscura que su
   vecina. Es el cambio de mipmap: queda casi todo por encima de 12 kHz y es normal.

### Recetas
**Bajo de sierra clásico (house / synthwave)**
1. *Basic Shapes*, Position **67 %** (sierra): tiene todos los armónicos, así que se oye incluso en altavoces pequeños.
   Es la solución "de verdad" al problema del sub de la Fase 1.
2. Voices **1**: un bajo casi siempre es monofónico. Así dos notas no se solapan y no embarran los graves.
3. Attack **2 ms**, Decay **300 ms**, Sustain **60 %**, Release **60 ms**: golpe inicial, cuerpo después y un corte
   limpio para dejar sitio al bombo.
4. Velocity **30 %**: algo de expresión, sin que el volumen del bajo salte.
5. Piano Roll: corcheas entre **C3 (65 Hz)** y **C4 (131 Hz)**.
6. *Por qué funciona:* la sierra es brillante y "zumba". Sin filtro todavía es algo áspera: en la Fase 4 aprenderás
   a domarla con un low-pass, que es exactamente lo que hacen los bajos de synthwave.

**Pad de "coro" que se mueve**
1. *Vowels*, Position **0 %** (vocal A).
2. Attack **800 ms**, Decay **1 s**, Sustain **80 %**, Release **2.5 s**, Voices **8**, Velocity **20 %** (la receta de pad de la Fase 2).
3. Acordes largos entre **C4 y C6** (4 compases cada uno).
4. Automatiza Position de 0 % a 100 % a lo largo de 8 compases (clip de automatización).
5. *Por qué funciona:* los formantes son lo que el oído asocia con una **voz**. Al moverlos, el pad "dice" vocales
   y nunca suena estático. Por encima de C6 los formantes suben tanto que se pierde el efecto vocal: por eso se
   queda en el registro medio.

### Reto sin receta
Recrea el **lead de una consola de 8 bits (NES / Game Boy)** para una melodía rápida a 150 BPM.
Esas consolas no tenían una sierra: usaban **ondas de pulso** con anchos fijos de **50 %, 25 % y 12.5 %**, y el
cambio de ancho era parte del "instrumento". El sonido debe ser seco, sin cola y con el mismo volumen en todas
las notas. Extra: consigue que la primera nota de cada frase suene más "fina" que las demás.
Inténtalo primero; las pistas vendrán después.

### Vocabulario
- **Wavetable.** Colección de ciclos de onda que se recorre con una perilla.
  *Ejemplo:* los bajos "que hablan" del dubstep (Skrillex) se hacen moviendo la posición de un wavetable.
- **Frame.** Uno de los ciclos guardados en el wavetable.
- **Morphing.** Transición continua entre dos timbres. *Ejemplo:* un pad que pasa de oscuro a brillante sin filtro.
- **Sierra / cuadrada / triángulo / pulso.** Las formas de onda clásicas de los sintes analógicos.
  *Ejemplo:* la sierra es el sonido de los leads de trance; la cuadrada, el de la música de videojuegos antiguos.
- **Ancho de pulso (PWM si se modula).** Proporción del ciclo en la que la onda está "arriba".
  *Ejemplo:* los pads y bajos de los Juno de Roland en los 80.
- **Hard sync.** Un oscilador que se reinicia al ritmo de otro y crea armónicos resonantes que se mueven.
  *Ejemplo:* el lead de "Kiss on My List" (Hall & Oates) o muchos leads de electro.
- **Formante.** Zona del espectro reforzada por una resonancia; define las vocales.
  *Ejemplo:* un talk box o el efecto "wah" de una guitarra.
- **Nyquist.** La frecuencia máxima representable: la mitad del sample rate (22.05 kHz a 44.1 kHz).
- **Aliasing.** Frecuencias falsas que aparecen cuando un armónico pasa de Nyquist y se refleja hacia abajo.
  *Ejemplo:* el "brillo sucio" de algunos plugins baratos en notas agudas, o el sonido *lo-fi* buscado a propósito en un bitcrusher.
- **Band-limited.** Una señal a la que se le quitaron los armónicos que causarían aliasing.
- **Espectro.** Lo que muestra el analizador: qué frecuencias hay y con qué fuerza.
- **Interpolación.** Calcular un valor entre dos muestras conocidas.

### Retos completados
- [ ] Reto Fase 3 — lead de 8 bits
