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

---

## Fase 4 — Filtros

### Conceptos aprendidos
- **Síntesis sustractiva.** Se empieza con una onda llena de armónicos (sierra, cuadrada) y se **quitan** los que
  sobran con un filtro. Es la receta de casi todos los sintes clásicos (Moog, Juno, TB-303) y sigue siendo la base
  de Serum y Vital: el wavetable da el "material" y el filtro lo esculpe.
- **Tipos de filtro.**
  - **Low-pass (LP):** deja pasar los graves y quita los agudos. Es el más usado: oscurece, suaviza, "cierra" el sonido.
  - **High-pass (HP):** lo contrario, quita los graves. Adelgaza, deja el sonido "sin cuerpo" (radio, teléfono).
  - **Band-pass (BP):** solo deja una franja alrededor del cutoff. Suena nasal, como a través de un tubo.
- **Cutoff.** La frecuencia donde el filtro empieza a actuar. Justo en el cutoff (sin resonancia) la señal ya baja
  **−3 dB**; a partir de ahí cae cada vez más.
- **Pendiente (12 o 24 dB por octava).** Cuánto baja el volumen por cada octava más allá del cutoff.
  Con 12 dB, un armónico 3 octavas por encima baja 36 dB; con 24 dB baja 72 dB. El de 12 dB suena más suave y
  "abierto"; el de 24 dB, más oscuro y definido (el sonido Moog). Un filtro de 12 dB tiene **2 polos**; el de 24 dB,
  **4 polos** (en el código: dos filtros de 12 dB en cadena).
- **Resonancia (Q).** Realimenta la señal alrededor del cutoff y crea un **pico**: esas frecuencias se refuerzan
  en vez de bajar. Con poca resonancia el sonido gana "presencia"; con mucha, el filtro "canta" una nota propia y al
  moverlo se oye el típico "wiuuu". En Undertow, a resonancia máxima el pico está a **+15 dB**.
  Para que el volumen no se dispare, los graves bajan ~6 dB al subir la resonancia (igual que en un filtro Moog).
- **Por qué un filtro ZDF/TPT.** Un filtro analógico es un circuito con realimentación. La forma "fácil" de
  digitalizarlo mete un retraso de una muestra en esa realimentación, y eso desafina el cutoff y lo vuelve inestable
  al modular rápido. El ZDF (*zero-delay feedback*) resuelve el circuito "tal cual" en cada muestra. Resultado medido:
  el filtro digital coincide con la teoría con un error de **0.0001 dB**, el cutoff cae exactamente donde se pide
  (también a 15 kHz) y no explota aunque el cutoff salte en cada muestra (clave para la Fase 5).
- **Drive (saturación).** Sube el volumen **antes** del filtro y lo pasa por una curva *tanh* que redondea los picos,
  como un amplificador saturado. Añade armónicos nuevos (más "mordida", más agresivo) y comprime el sonido.
  Como está antes del filtro, el low-pass después "doma" los armónicos más agudos que crea la saturación.
- **Key tracking.** Con 0 %, el cutoff es el mismo para todas las notas: las graves suenan brillantes y las agudas,
  apagadas (pierden armónicos). Con 100 %, el cutoff sube una octava por cada octava de la nota (la referencia es
  **C5**): todo el teclado suena con el mismo "color".
- **Límite actual:** el drive todavía no tiene oversampling. Con drive alto en notas muy agudas (≥ C8) puede oírse
  algo de aliasing (tonos metálicos). En graves y medios queda muy por debajo del sonido. Se resolverá en la Fase 7/8.

### Qué hace cada control al sonido
| Control | Qué se oye | En el analizador / osciloscopio |
|---|---|---|
| **On** | Enciende el filtro (con un fundido de 5 ms, sin clic). Apagado, el sonido es el de la Fase 3. | La curva naranja del visor pasa de una línea plana tenue a la forma del filtro. |
| **Type: Low Pass** | Bajar el cutoff oscurece: brillante → cálido → apagado → "bajo el agua". | En el EQ desaparecen las líneas de la derecha (agudos). En Wave Candy la sierra se redondea hasta parecer un seno. |
| **Type: High Pass** | Subir el cutoff quita el cuerpo: lleno → delgado → "de radio" → solo un siseo. | Desaparecen las líneas de la izquierda. La fundamental se va primero. |
| **Type: Band Pass** | Solo una franja: nasal, "de teléfono" o "de tubo". Moverlo suena a un pedal wah. | Solo queda una "montaña" de armónicos alrededor del cutoff. |
| **Slope 12 / 24 dB** | 12 dB: el corte es suave y queda algo de brillo. 24 dB: el corte es más marcado y oscuro. | Con 24 dB la curva del visor cae el doble de empinada. |
| **Cutoff** | El "brillo" del sonido. Es la perilla más expresiva de un sinte: moverla es el "filter sweep". | El punto donde la curva empieza a bajar. |
| **Resonance** | 0 %: neutro. 30–50 %: más presencia y carácter. 70–100 %: silbido, "wiuuu" al mover el cutoff, sonido ácido. | Un pico en el cutoff: el armónico que cae ahí sobresale sobre los demás. |
| **Drive** | Más cálido y denso al principio; con más drive, agresivo y comprimido. Las notas suaves y fuertes se igualan. | Aparecen armónicos nuevos (en un seno: 3.º, 5.º…). En el osciloscopio los picos se aplanan. |
| **Key Track** | 0 %: las notas agudas suenan más apagadas que las graves. 100 %: todas suenan igual de brillantes. | Con 100 %, al subir una octava la "montaña" de armónicos se mueve una octava junto con la nota. |

### Ejercicio de escucha guiado
1. Montaje de siempre: **Fruity Parametric EQ 2** (analizador) y **Wave Candy** en el canal del Mixer.
   *Basic Shapes*, Position **67 %** (sierra). Attack 5 ms, Sustain 100 %, Release 150 ms.
2. **El barrido:** enciende el filtro (**On**, Low Pass, 24 dB, Resonance 0 %). Mantén un **C4** (130.8 Hz) y baja
   el Cutoff despacio de 20 kHz a 50 Hz. En el EQ mira cómo se "apagan" las líneas de derecha a izquierda. Cuando
   el cutoff pasa por debajo de 131 Hz, hasta la fundamental empieza a bajar y el sonido casi desaparece.
3. **12 contra 24 dB:** deja el Cutoff en **800 Hz** y alterna Slope entre 12 y 24 dB. El de 12 dB deja un
   "aire" brillante; el de 24 dB es más redondo y oscuro. Compara las dos curvas en el visor del plugin.
4. **Resonancia:** Cutoff 800 Hz, Slope 24 dB. Sube Resonance a 0, 30, 60 y 90 %. En cada paso barre el Cutoff
   entre 200 Hz y 3 kHz. Con 90 % oirás cómo el filtro "canta" cada armónico que atraviesa (como una escala de
   armónicos). En el EQ, el armónico que cae en el pico sobresale.
5. **High-pass y band-pass:** cambia a High Pass y sube el Cutoff de 20 Hz a 2 kHz: el sonido pierde el cuerpo
   hasta sonar como una radio. Luego Band Pass con Resonance 50 %: barre el Cutoff y escucha el efecto "wah".
6. **Key tracking:** Low Pass 24 dB, Cutoff **500 Hz**, Key Track **0 %**. Toca **C3 (65 Hz)**, **C5** y **C7** seguidas:
   la grave suena brillante y la aguda, apagada. Sube Key Track a **100 %** y repite: ahora las tres tienen el
   mismo color (solo cambia la altura).
7. **Drive:** Low Pass 24 dB, Cutoff 20 kHz, Position **0 %** (seno). Sube Drive de 0 a 100 % mientras miras el
   osciloscopio: el seno se aplana hasta parecer una cuadrada y en el EQ aparecen los armónicos impares.
   Después vuelve a la sierra y prueba Drive 40 % con Cutoff 600 Hz: el bajo suena más grueso y "empuja".
8. **Artefactos a buscar:** clics al encender/apagar el filtro, al cambiar de tipo o de pendiente (no debería
   haber ninguno); zipper al girar Cutoff rápido (no debería haberlo); saltos de volumen extraños al subir la
   resonancia; y con Drive 100 % en notas por encima de C8, algún tono metálico (es el límite conocido).

### Recetas
**Bajo analógico "Moog" (synthwave, funk, house)**
1. *Basic Shapes*, Position **67 %** (sierra). Voices **1** y Velocity **30 %**, como en el bajo de la Fase 3.
2. Filtro **On**, **Low Pass 24 dB**, Cutoff **450 Hz**. Con 24 dB la sierra pierde su "zumbido" y queda un bajo
   redondo, pero todavía con algo de ataque.
3. Resonance **25 %**: un pequeño pico cerca del cutoff le da "voz" sin que silbe.
4. Drive **30 %**: engorda el sonido y lo iguala en volumen, como un circuito analógico empujado.
5. Key Track **50 %**: las notas agudas del riff no se apagan, y las graves no se vuelven demasiado brillantes.
6. Attack **2 ms**, Decay **300 ms**, Sustain **60 %**, Release **60 ms**.
7. Piano Roll: corcheas entre **C3 (65 Hz)** y **C4 (131 Hz)**, con alguna octava arriba.
8. *Por qué funciona:* la sierra da los armónicos y el low-pass de 24 dB deja solo los primeros. Esos son los que
   dan "cuerpo" en un bajo. El drive añade la calidez del hardware.

**Línea ácida (acid house, estilo TB-303)**
1. *Basic Shapes*, Position **100 %** (cuadrada) o **67 %** (sierra): la 303 tenía las dos.
2. Voices **1**. Attack **1 ms**, Decay **200 ms**, Sustain **40 %**, Release **40 ms**: notas cortas y con golpe.
3. Filtro **Low Pass 24 dB**, Cutoff **400 Hz**, Resonance **75 %**, Drive **45 %**, Key Track **30 %**.
4. Piano Roll a **125 BPM**: semicorcheas repetitivas sobre **C3–C4**, alternando octavas y alguna nota suelta.
5. Clic derecho en Cutoff → *Create automation clip*. Dibuja una curva que suba de 250 Hz a 3 kHz y vuelva a bajar
   en 8 compases. **Esa curva ES el sonido acid**.
6. *Por qué funciona:* la resonancia alta hace que el filtro "chille" en el cutoff y el drive endurece ese chillido.
   Al mover el cutoff, el pico va pasando por los armónicos y crea el "wau-wau" característico.
   (En la Fase 5 una envolvente moverá el cutoff en cada nota: el "squelch" completo.)

### Reto sin receta
El truco más famoso de la música electrónica: **un pad que suena "detrás de una pared"** (como la música de la
fiesta del vecino) durante un build-up de 8 compases, y **"se abre la puerta"** justo en el drop.
Debe sonar apagado y lejano al principio, sin perder las notas, y abrirse con tensión creciente.
Extra: que en los últimos 2 compases antes del drop haya algo que "silbe" y suba.
Inténtalo primero; las pistas vendrán después.

### Vocabulario
- **Síntesis sustractiva.** Crear sonidos quitando armónicos a una onda rica. *Ejemplo:* casi todos los bajos y
  pads de los 80 (Minimoog, Juno-106).
- **Filtro low-pass / high-pass / band-pass.** Dejan pasar los graves / los agudos / una franja.
  *Ejemplo:* el sonido "de radio" de una voz en un intro es un high-pass + low-pass (una banda estrecha).
- **Cutoff (frecuencia de corte).** Donde el filtro empieza a actuar. *Ejemplo:* el "filter sweep" de un
  build-up de EDM es el cutoff subiendo.
- **Pendiente (dB/octava) y polos.** Cuánto baja el sonido por octava. 2 polos = 12 dB, 4 polos = 24 dB.
  *Ejemplo:* el filtro de 24 dB del Minimoog es la razón de su bajo tan redondo.
- **Resonancia (Q).** Pico de volumen en el cutoff. *Ejemplo:* el "squelch" de la TB-303 en "Acid Tracks" (Phuture).
- **Auto-oscilación.** Cuando la resonancia es tan alta que el filtro suena solo, como un seno.
  (Undertow todavía no llega a auto-oscilar.) *Ejemplo:* los "zaps" y silbidos de los sintes analógicos.
- **Drive / saturación.** Amplificar hasta que la señal se "aplana" y gana armónicos.
  *Ejemplo:* el bajo gordo y sucio de Daft Punk en "Da Funk".
- **Key tracking.** Hacer que el cutoff siga a la nota. *Ejemplo:* en un piano eléctrico o un pluck que tiene el
  mismo brillo en todo el teclado.
- **Filter sweep.** Mover el cutoff a lo largo del tiempo. *Ejemplo:* los intros filtrados del French house
  (Stardust, "Music Sounds Better With You").
- **ZDF / TPT.** Una forma de programar filtros digitales que se comportan como los analógicos, incluso al moverlos rápido.

### Retos completados
- [ ] Reto Fase 4 — pad "detrás de una pared" que se abre en el drop

---

## Fase 5 — Modulación: envolventes, LFOs y matriz

### Conceptos aprendidos
- **Modulación = algo que mueve una perilla por ti.** Hasta ahora, si querías que el cutoff subiera tenías que girarlo
  o dibujar una automatización en FL. Una **fuente de modulación** es un "brazo robot" que lo gira solo, en cada nota,
  de forma repetible. Casi todo lo que hace que un sinte suene "vivo" (un pluck, un wobble, un vibrato, un pad que
  respira) es modulación.
- **Ruta = fuente → destino × amount.** La **matriz de modulación** tiene 8 rutas. Cada una conecta una fuente
  (Env 2, LFO 1, Velocity…) con un destino (Cutoff, Pitch…) con una cantidad (**amount**, de −100 % a +100 %).
  El valor final es: **perilla + suma de (fuente × amount)** de todas las rutas que van a ese destino.
  - Amount **positivo** empuja el destino hacia arriba; **negativo**, hacia abajo (la misma envolvente puede abrir o
    cerrar el filtro).
  - La perilla es el **punto de partida**: con Cutoff 300 Hz y Env 2 → Cutoff +40 %, el filtro sale de 300 Hz,
    sube 4 octavas (a 4.8 kHz) y vuelve.
- **Escala del amount (100 % = recorrido completo de la perilla):**
  | Destino | Qué significa el amount |
  |---|---|
  | Osc A Position | 100 % = de 0 % a 100 % de la tabla. |
  | Osc A Pitch | 100 % = **±24 semitonos**. 50 % = una octava; **4.17 % = 1 semitono**; ~1.25 % ≈ un vibrato suave. |
  | Filter Cutoff | 100 % = **10 octavas**. Muy fácil de pensar: **cada 10 % = 1 octava**. |
  | Filter Resonance / Drive | 100 % = toda la perilla. |
  | Volume | −100 % = silencio, +100 % = el doble de volumen (+6 dB). |
  Truco: los valores pequeños (como un vibrato de 1.25 %) se escriben mejor que se arrastran: doble clic en la
  casilla del número y teclea `1.25`. **Doble clic en la barra** vuelve el amount a 0 %.
- **Unipolar y bipolar.** Las envolventes y la velocity van de **0 a 1** (unipolares: solo empujan en una dirección).
  Los LFO van de **−1 a +1** (bipolares: suben y bajan alrededor de la perilla). Por eso un LFO sobre el cutoff
  "abre y cierra" alrededor del valor de la perilla, y una envolvente solo lo abre (o solo lo cierra con amount negativo).
- **Envolventes de modulación (Env 2 y Env 3).** Son ADSR iguales a la de volumen, pero no mueven el volumen: mueven
  lo que tú conectes. La Env 1 (la de amplitud) también se puede usar como fuente.
- **LFO (Low Frequency Oscillator).** Un oscilador demasiado lento para oírse como nota (0.02 a 40 Hz) que se usa
  para mover parámetros de forma cíclica.
  - **Formas:** *Sine* (suave, orgánico), *Triangle* (lineal, parejo), *Saw Up/Down* (rampa que sube/baja y salta),
    *Square* (dos estados: on/off), *Sample & Hold* (un valor al azar por ciclo: "computadora que piensa").
  - **Rate / Sync.** Con *Sync* apagado se ajusta en Hz. Con *Sync* encendido se ajusta en figuras musicales
    (1/4 = una negra, 1/8 T = tresillo de corchea, 1/4 D = negra con puntillo…) y sigue el tempo de FL.
  - **Modos.** *Retrigger*: cada nota reinicia su LFO (todas las notas "respiran" igual desde que empiezan).
    *Free*: un solo reloj para todas las notas; con Sync, además, **ligado al compás de la canción** (el wobble cae
    siempre en el pulso, aunque empieces a reproducir a mitad de compás). *One Shot*: hace un solo ciclo y se para
    (una "envolvente dibujada").
- **Fuentes MIDI.** *Velocity* (qué tan fuerte tocas), *Key* (qué nota: 0 en C5, sube hacia los agudos),
  *Mod Wheel* (la rueda de modulación, CC 1) y *Aftertouch* (presión sobre la tecla ya pulsada).
  Dato curioso: **Key → Cutoff al 50 % es exactamente el Key Track al 100 %** de la Fase 4. El key tracking era
  una modulación "precableada".
- **Por qué nada hace clic.** Un LFO cuadrado o un Sample & Hold saltan de golpe; conectado a Volume, ese salto sería
  un clic. Esas fuentes se suavizan 1 ms (inaudible como "lentitud", suficiente para no chasquear). Las envolventes
  **no** se suavizan (ya son continuas): un attack de 1 ms llega en 1 ms. Y si cambias una ruta con el amount subido,
  la ruta vieja se apaga en 5 ms antes de que entre la nueva.
- **Límite resuelto de la Fase 3.** Con el pitch modulado (vibrato, caídas de tono) una nota puede cruzar el límite
  entre dos mipmaps. Antes, al cruzarlo, la octava más aguda del sonido desaparecía de golpe. Ahora, en los últimos
  2 semitonos antes de cada límite, se mezclan los dos niveles: el brillo cambia de forma continua.

### Qué hace cada control al sonido
| Control / ruta | Qué se oye | En el analizador / osciloscopio |
|---|---|---|
| **Env 2 → Cutoff** (amount +) | Cada nota empieza brillante y se oscurece: el "pluck", el "squelch" del acid, el ataque de un bajo. | Las líneas de los agudos aparecen al principio de cada nota y se apagan con el decay. |
| **Decay de la Env 2** | Corto (50–150 ms): percusivo, "plick". Largo (1–2 s): "wooow" que se cierra despacio. | La velocidad con la que se apagan los agudos. |
| **Sustain de la Env 2** | 0 %: el filtro se cierra del todo. 50 %: se queda a medio abrir mientras mantienes la nota. | Cuántos agudos quedan con la nota sostenida. |
| **Env 2 → Pitch** (amount +) | La nota empieza más aguda y "cae" a su tono: zap, láser, bombo sintético. | En el osciloscopio el ciclo se ve primero apretado y luego se ensancha. |
| **LFO → Cutoff** | El brillo sube y baja cíclicamente: lento = "wah" que respira; 1/8 o 1/16 = wobble de dubstep. | Los agudos aparecen y desaparecen al ritmo del LFO. |
| **LFO → Pitch** (1–4 %) | Vibrato: como un cantante o un violinista. Mucho amount = sirena. | En el EQ, cada armónico se mueve de lado a lado. |
| **LFO → Volume** | Trémolo (seno) o "trance gate" (cuadrado, sincronizado): el sonido se corta en ritmo. | En Wave Candy el volumen late como un corazón. |
| **LFO → Position** | El timbre cambia solo: con *Vowels*, el sinte "habla" (a-e-i-o-u). | La forma de onda del visor no cambia (muestra la perilla), pero en el osciloscopio sí. |
| **Forma del LFO** | Sine: suave. Triangle: parejo. Saw: rampa y salto (sube-sube-¡cae!). Square: dos estados. S&H: saltos al azar. | La curva azul del visor y el punto blanco que la recorre. |
| **Rate / Division** | Más rápido = más nervioso. Sync hace que el movimiento sea parte del ritmo. | El punto blanco del visor va más rápido. |
| **Free / Retrigger** | Retrigger: cada nota empieza su ciclo igual. Free: todas las notas pulsan juntas, pegadas al compás. | Con Retrigger el punto vuelve al inicio con cada nota. |
| **Velocity → Cutoff / Volume** | Tocar más fuerte = más brillante / más fuerte, como un instrumento real. | Más agudos en las notas acentuadas. |
| **Key → Cutoff** | El brillo sigue a la nota (el key tracking de la Fase 4, pero con amount negativo también puedes invertirlo). | La "montaña" de armónicos se mueve con la nota. |
| **Mod Wheel / Aftertouch → algo** | Tú controlas el movimiento en vivo con el teclado MIDI. | — |

### Ejercicio de escucha guiado
Montaje: **Fruity Parametric EQ 2** y **Wave Candy** en el canal del Mixer, como siempre. En Undertow: *Basic Shapes*,
Position **67 %** (sierra), Voices **8**, Env 1: Attack 5 ms, Decay 500 ms, Sustain 100 %, Release 150 ms.
Filtro **On**, **Low Pass 24 dB**, Cutoff **300 Hz**, Resonance **30 %**. Tempo del proyecto: **120 BPM**.
Todo lo nuevo está en la pestaña **Modulación** (arriba a la derecha).

1. **La envolvente del filtro.** Ruta 1: **Env 2 → Filter Cutoff, +40 %**. Env 2: Attack 1 ms, Decay **300 ms**,
   Sustain **0 %**, Release 150 ms. Toca **C4** varias veces: cada nota hace "piuu". Mueve el Decay de la Env 2 a
   **60 ms** (pluck seco) y a **1.5 s** (se cierra despacio). Sube el Sustain a **50 %** y mantén la nota: el filtro
   se queda a medio abrir. Cambia el amount a **−40 %** y sube el Cutoff a 5 kHz: ahora la nota empieza oscura y
   se abre (al revés).
2. **LFO sobre el filtro.** Borra la ruta 1 (Source *None*). Ruta 1: **LFO 1 → Filter Cutoff, +20 %** (±2 octavas).
   LFO 1: *Sine*, **Sync apagado**, Rate **0.5 Hz**. Mantén un acorde: el brillo "respira". Sube el Rate a 2, 6 y 12 Hz:
   de "wah" lento a wobble y a un trémolo de brillo. Prueba las 6 formas con Rate 2 Hz y mira el punto del visor.
3. **Sincronizado al tempo.** Enciende **Sync**, Division **1/4**. Pon un patrón de batería sencillo (bombo en cada
   negra) y dale a Play: el wobble cae con el bombo. Prueba 1/8, 1/16 y **1/8 T** (tresillos: suena "a galope").
4. **Free contra Retrigger.** Mode **Retrigger**, Division 1/2. Toca un acorde nota por nota, con medio segundo entre
   cada una: cada nota tiene su propio ciclo y se "desordenan". Cambia a **Free** y repite: ahora todas pulsan juntas.
5. **Vibrato.** Borra las rutas. Ruta 1: **LFO 2 → Osc A Pitch** y escribe **1.25** en la casilla del amount
   (≈ ±0.3 semitonos). LFO 2: *Sine*, Sync apagado, Rate **5.5 Hz**, Mode Retrigger. Filtro abierto (Cutoff 20 kHz).
   Toca **A5 (440 Hz)**: vibrato de cantante. Sube a **4.17 %** (±1 semitono) y a **25 %**: sirena. Mira en el EQ cómo los
   armónicos se mueven de lado a lado.
6. **Sample & Hold.** LFO 2 en *Sample & Hold*, Sync, **1/16**. Ruta 1 cámbiala a **LFO 2 → Filter Cutoff, +25 %**, con
   Resonance **60 %** y Cutoff 800 Hz: el sonido "de computadora de película".
7. **Trance gate.** Ruta 1: **LFO 1 → Volume, −100 %**, LFO 1 *Square*, Sync, **1/16**, Mode Free. Mantén un acorde con
   Play: el pad se corta en semicorcheas. Debe sonar como un corte limpio, **sin clics**. Fíjate en algo: el LFO es
   bipolar, así que con −100 % el volumen alterna entre **0** (silencio) y **el doble** (+6 dB). Baja el Master 6 dB
   para compensar. (Con −50 % el volumen alterna entre la mitad y 1.5 veces: un trémolo en vez de un corte.)
8. **Velocity.** Ruta 2: **Velocity → Filter Cutoff, +30 %**. En el Piano Roll pon varias notas con velocities
   distintas (panel de abajo): las fuertes suenan 3 octavas más brillantes.
9. **Artefactos a buscar:** clics al cambiar una ruta con el amount alto, al usar LFO cuadrado o S&H sobre Volume o
   Cutoff (no debería haber); saltos de brillo al hacer vibrato en notas muy agudas (≥ C8), que ya no deberían oírse;
   y que el wobble sincronizado no se "desfase" del bombo después de varios compases.

### Recetas
**1. Acid con "squelch" (acid house, estilo TB-303) — completa la receta de la Fase 4**
1. Parte de la *Línea ácida* de la Fase 4: *Basic Shapes* 67 %, Voices **1**, Filtro **Low Pass 24 dB**,
   Resonance **75 %**, Drive **45 %**, Key Track **30 %**. Baja el Cutoff a **250 Hz**.
2. Env 1: Attack **1 ms**, Decay **200 ms**, Sustain **40 %**, Release **40 ms**.
3. Env 2: Attack **1 ms**, Decay **180 ms**, Sustain **0 %**, Release **40 ms**.
   Ruta 1: **Env 2 → Filter Cutoff, +40 %** (cada nota abre el filtro 4 octavas, de 250 Hz a 4 kHz, y lo cierra).
4. Ruta 2: **Velocity → Filter Cutoff, +20 %**. En el Piano Roll deja casi todas las notas con velocity baja y pon
   "acentos" (velocity alta) en 1 de cada 3 o 4: esas notas "chillan" hasta 2 octavas más.
5. Piano Roll a **125 BPM**: semicorcheas sobre **C3–C4** con alguna octava arriba. Automatiza además el Cutoff
   (como en la Fase 4) de 150 Hz a 1 kHz a lo largo de 8 compases.
6. *Por qué funciona:* el "squelch" de la 303 es una envolvente rápida sobre un filtro muy resonante: el pico de
   resonancia barre los armónicos en cada nota. La automatización lenta del cutoff mueve **dónde** empieza ese
   barrido, y la velocity decide **qué notas** gritan.

**2. Wobble bass (dubstep, 140 BPM)**
1. *Basic Shapes*, Position **85 %** (entre sierra y cuadrada: mucho cuerpo y armónicos impares). Voices **1**.
2. Env 1: Attack **5 ms**, Decay 500 ms, Sustain **100 %**, Release **80 ms**.
3. Filtro **Low Pass 24 dB**, Cutoff **300 Hz**, Resonance **45 %**, Drive **50 %**, Key Track 0 %.
4. LFO 1: *Sine*, Mode **Free**, **Sync**, Division **1/8**. Ruta 1: **LFO 1 → Filter Cutoff, +30 %**
   (±3 octavas: de 37 Hz a 2.4 kHz). Ruta 2: **LFO 1 → Osc A Position, −15 %** (cuando el filtro se abre, la onda
   se vuelve más sierra: el "wub" gana mordida).
5. Piano Roll a **140 BPM**: notas largas en **F3 (87 Hz)** y **G#3 (104 Hz)**. Clic derecho en *LFO 1 Division* →
   *Create automation clip*: cambia entre 1/8, 1/16 y 1/8 T cada compás. **Ese cambio de división es el "fraseo"
   del wobble.**
6. *Por qué funciona:* el LFO abre y cierra el filtro en ritmo ("wub-wub"). En modo Free + Sync el wobble está
   pegado al compás: siempre cae con la batería, aunque la nota empiece tarde.

**3. Pad que respira (ambient, cinemático)**
1. *Vowels*, Position **50 %**. Voices **8**. Env 1: Attack **800 ms**, Decay 1 s, Sustain **90 %**, Release **1.5 s**.
2. Filtro **Low Pass 12 dB**, Cutoff **1.5 kHz**, Resonance **20 %**.
3. LFO 1: *Triangle*, Mode **Free**, Sync apagado, Rate **0.15 Hz** (un ciclo cada ~7 s). Ruta 1:
   **LFO 1 → Osc A Position, +40 %**: el pad recorre las vocales despacio (de 10 % a 90 % de la tabla).
4. Env 3: Attack **2 s**, Decay 1 s, Sustain **100 %**, Release 1.5 s. Ruta 2: **Env 3 → Filter Cutoff, +15 %**:
   al mantener el acorde, el pad se abre 1.5 octavas poco a poco.
5. LFO 2: *Sine*, Retrigger, Sync apagado, Rate **4.5 Hz**. Ruta 3: **LFO 2 → Osc A Pitch, 0.8 %** (vibrato casi
   imperceptible, ±0.2 semitonos: le quita lo "de máquina").
6. Piano Roll: acordes largos (4 compases cada uno) en la zona **C4–C6**.
7. *Por qué funciona:* un sonido "vivo" nunca está quieto. Tres movimientos lentos y a velocidades distintas (7 s,
   2 s y 4.5 Hz) no se repiten nunca igual, y el oído lo percibe como algo orgánico.

### Reto sin receta
**El "sidechain falso".** En el EDM, los pads y bajos "bombean": bajan de golpe cuando suena el bombo y vuelven a
subir antes del siguiente (se hace con un compresor con sidechain). Tu reto: conseguir ese bombeo **solo con
Undertow**, sin ningún plugin extra, sobre un pad de acordes a **128 BPM** con un bombo en cada negra.
El volumen debe caer justo en el bombo y recuperarse de forma suave (no un corte seco como el trance gate).
Extra: que el filtro también se cierre un poco en cada golpe, para que el bombeo se note más.
Inténtalo primero; las pistas vendrán después.

### Vocabulario
- **Modulación.** Hacer que un parámetro cambie solo. *Ejemplo:* el "wah" automático de casi cualquier pad de trance.
- **Fuente / destino / amount.** Qué mueve, qué se mueve y cuánto. *Ejemplo:* "Env 2 → Cutoff al 40 %" es la frase
  típica en un tutorial de Serum o Vital.
- **Matriz de modulación.** La tabla de rutas fuente → destino. *Ejemplo:* la pestaña *Matrix* de Serum.
- **Envolvente de filtro.** Una envolvente conectada al cutoff. *Ejemplo:* el "bwow" de los bajos de synthwave y
  el pluck de "Faded" (Alan Walker).
- **LFO.** Oscilador lento que mueve parámetros. *Ejemplo:* el wobble de "Scary Monsters and Nice Sprites" (Skrillex).
- **Rate / tempo sync.** Velocidad del LFO, en Hz o en figuras musicales. *Ejemplo:* los wobbles a 1/8 y 1/16
  del dubstep.
- **Retrigger / free-running.** El LFO reinicia con cada nota / corre libre para todas. *Ejemplo:* un pad con
  LFO libre "respira" siempre al mismo ritmo aunque cambien los acordes.
- **One shot.** Un LFO que da una sola vuelta. *Ejemplo:* efectos de "subida" o "caída" dibujados a mano en Serum.
- **Sample & Hold.** Valores al azar, uno por ciclo. *Ejemplo:* los "bleeps" de computadora de las películas de
  ciencia ficción de los 70–80.
- **Unipolar / bipolar.** Que solo sube (0..1) o que sube y baja (−1..+1). *Ejemplo:* una envolvente es unipolar;
  un LFO, bipolar.
- **Vibrato.** Oscilación del tono. *Ejemplo:* la voz de un cantante de ópera o el violín.
- **Trémolo.** Oscilación del volumen. *Ejemplo:* la guitarra de "Boulevard of Broken Dreams" (Green Day).
- **Trance gate.** Volumen cortado en ritmo con un LFO cuadrado. *Ejemplo:* los pads entrecortados del trance de
  los 2000 (Tiësto, ATB).
- **Wobble.** LFO rápido y sincronizado sobre el cutoff de un bajo. *Ejemplo:* todo el brostep de 2010–2012.
- **Mod wheel / aftertouch.** Controles expresivos del teclado MIDI. *Ejemplo:* el vibrato que un tecladista añade
  con la rueda al final de una nota larga.

### Retos completados
- [ ] Reto Fase 5 — sidechain falso con modulación

---

## Fase 6 — Dos osciladores, sub, ruido y unison

### Conceptos aprendidos
- **Capas (layering).** Dos osciladores sonando a la vez simplemente se **suman**. Si están afinados a intervalos
  "limpios" (octava, quinta) el oído los funde en un solo sonido más rico; si están casi a la misma frecuencia,
  aparecen batidos (siguiente punto). Casi todos los sonidos grandes de un sinte son 2–4 capas.
- **Afinación en tres escalas.** Una **octava** duplica la frecuencia (×2). Un **semitono** es la 12.ª parte de una
  octava: ×2^(1/12) ≈ ×1.0595. Un **cent** es la centésima parte de un semitono. Octave/Semi eligen el intervalo;
  Fine es para desafinar un poco a propósito.
- **Batidos (beating).** Dos ondas de frecuencias muy cercanas se refuerzan y se cancelan alternativamente: el volumen
  "pulsa" **|f1 − f2|** veces por segundo. Con 440 Hz y 442 Hz: 2 pulsos por segundo. Es la base de todo el "grosor"
  de un sinte: un pulso lento suena a coro que respira; muchos pulsos rápidos a la vez suenan a masa densa.
  Los mismos cents dan batidos más rápidos en notas agudas (hay más Hz entre las dos ondas): por eso un detune
  que suena bien en un pad grave puede sonar "desafinado" en un lead agudo.
- **Unison.** El oscilador toca N copias de la misma onda, cada una un poco desafinada respecto a las demás. Es
  como pasar de un violín a una sección de cuerdas: nadie toca exactamente igual y eso suena "grande".
  Tres decisiones del código que tienen consecuencias sonoras:
  1. **Detune en curva.** La mitad baja de la perilla (hasta ±25 cents) es la zona fina del coro; la mitad alta
     llega a ±100 cents (1 semitono), ya claramente desafinado. Las copias quedan a distancias iguales en cents.
  2. **Fases al azar.** Con varias copias, cada nota empieza con las copias en posiciones distintas del ciclo. Si
     todas salieran a la vez, el principio de cada nota tendría un pico fuerte y un barrido tipo "flanger".
     (Con 1 copia, la fase empieza siempre en 0: los bajos arrancan siempre con el mismo golpe.)
  3. **El volumen no sube con el número de copias.** Copias con fases distintas no suman amplitudes sino
     potencias: N copias tendrían √N veces más amplitud. Cada una se atenúa a 1/√N, así que 1 o 16 copias suenan
     igual de fuerte (medido: ±0.03 dB). Lo que cambia es el **carácter**, no el volumen.
- **Estéreo.** Si el canal izquierdo y el derecho son distintos, el oído percibe **ancho**. Width reparte las copias
  del unison de izquierda a derecha (en parejas: una grave y una aguda a cada lado, para que ningún lado suene
  "desafinado hacia abajo"). Pan mueve el oscilador entero.
- **Ley de paneo "equal power".** Al panear, la **potencia** total (L² + R²) se mantiene: un sonido no parece más
  débil por moverlo a un lado. En el centro L = R = 1, así que un sonido sin paneo suena igual que en la Fase 5.
- **Compatibilidad mono.** Muchos sistemas suman los dos canales (L + R): altavoces de discoteca, teléfonos, Bluetooth.
  Un unison muy abierto pierde algo de nivel y de "tamaño" al sumarse en mono (las copias de los lados, hasta −3 dB).
  Por eso los graves se mantienen **mono y centrados**: el sub de Undertow es mono siempre, y en los bajos el Width
  del unison se deja bajo.
- **Sub-oscilador.** Un oscilador simple (seno, triángulo…) una o dos octavas por debajo de la nota. No aporta
  "timbre": aporta **peso**, la fundamental que el oído siente en el pecho. Es la capa que sostiene un bajo.
- **Ruido.** Todas las frecuencias a la vez, al azar. **Blanco** = la misma energía en cada Hz (suena agudo, "shhh",
  porque en los agudos hay muchísimos más Hz). El **Color** lo inclina: oscuro = viento, mar, retumbo; brillante =
  hi-hat, aire, soplido. Mezclado en poca cantidad da "aire" a un pad o "chasquido" al ataque de un pluck.
- **Aliasing con unison.** El mipmap de la Fase 3 se elige para la copia **más aguda**: ninguna copia se refleja
  (medido: −92.8 dB con una sierra a ±1 semitono).
- **Coste de CPU.** Cada copia del unison se calcula aparte: 16 notas con los dos osciladores a 16 copias son 512
  osciladores sonando. Un supersaw normal (8 notas × 7 copias) cuesta ≈ 9 % de un núcleo; todo al máximo, ≈ 70 %.
  Úsalo donde se oye (pads, leads), no "por si acaso".

### Qué hace cada control al sonido
| Control | Qué se oye | En el analizador / osciloscopio |
|---|---|---|
| **Osc B On + Level** | Una segunda capa: más cuerpo o un timbre mezclado de dos tablas. | Se suman los armónicos de las dos ondas. |
| **Octave** | La capa sube o baja de registro. +1 = brillo "de órgano"; −1 = más cuerpo. | La montaña de armónicos se mueve al doble o a la mitad. |
| **Semi** | Intervalos: +7 = quinta ("power chord" en una tecla), +12 = octava, +3/+4 = acorde menor/mayor. | Dos series de armónicos intercaladas. |
| **Fine** | Poco (3–10 ct): batidos lentos, grosor analógico. Mucho (30–100 ct): desafinado, inquietante. | En el osciloscopio la forma "gira" y cambia lentamente. |
| **Pan** | Mueve el oscilador a la izquierda o a la derecha. | En un vectorscope, la línea se inclina. |
| **Unison** | 1 = una sola onda, limpia y "puntual". 3–7 = coro, sinte clásico. 9–16 = muro de sonido. | Cada armónico se vuelve un racimo de líneas muy juntas. |
| **Detune** | 0–15 %: coro sutil. 25–40 %: supersaw de trance. 60–100 %: desafinado, "sucio", disonante. | El racimo de cada armónico se ensancha. |
| **Width** | 0 %: todas las copias en el centro (mono). 100 %: el sonido llena de lado a lado (mejor con auriculares). | Vectorscope: de una línea vertical a una nube. |
| **Sub (forma)** | Sine: peso puro e invisible. Triangle: un poco más audible en altavoces pequeños. Saw/Square: bajo "retro". | Un pico enorme en 30–100 Hz; Saw/Square añaden armónicos. |
| **Sub Octave / Level** | −1: refuerza la nota. −2: sub-grave profundo (se siente más que se oye). | El pico baja una o dos octavas. |
| **Ruido Level** | Poco (5–15 %): aire y textura. Mucho: soplido, efecto. | Una "alfombra" sobre todo el espectro. |
| **Ruido Color** | 0 %: retumbo oscuro. 50 %: blanco, "shhh". 100 %: "tsss" fino, como un hi-hat. | La alfombra se inclina hacia los graves o hacia los agudos. |
| **Destinos nuevos** | Env → Noise Level: un "chasquido" de ruido en el ataque. LFO → Osc A Detune: el coro respira. Env → Global Pitch: caída de tono de todo el sinte a la vez. | — |

### Ejercicio de escucha guiado
Montaje: en el canal del Mixer de Undertow, **Fruity Parametric EQ 2** (para ver el espectro) y **Wave Candy**.
En Wave Candy, abre los presets y elige uno de **Vectorscope** (muestra el estéreo: una línea vertical = mono, una
nube = ancho). Usa **auriculares** para los pasos de estéreo. Todo lo nuevo está en la pestaña **Osciladores**.

1. **Batidos.** Osc A: *Basic Shapes* **0 %** (seno). Enciende **Osc B**: *Basic Shapes* 0 %, todo igual que A.
   Toca **A5 (440 Hz)**: suena igual, solo más fuerte. Sube **Fine de B** a **+1 ct**: aparece un "uaaa-uaaa" muy lento
   (≈ 0.25 pulsos por segundo). Pasa a **+5 ct** (≈ 1.3 pulsos/s), **+20 ct** (≈ 5/s) y **+50 ct** (≈ 13/s, ya rugoso).
   Con Fine en +20 ct toca **C3 (131 Hz)** y luego **C7 (2093 Hz)**: los mismos cents pulsan mucho más rápido en la nota
   aguda.
2. **Intervalos.** Fine de B a 0. Semi de B a **+7** y toca una nota: una quinta, el "power chord" del rock en una sola
   tecla. Prueba **+12**, **+3** y **+4**. Vuelve a 0 y pon **Octave +1** con Level de B al **40 %**: la capa aguda
   añade brillo sin que se oiga como "otra nota".
3. **Unison.** Apaga Osc B. Osc A: *Basic Shapes* **67 %** (sierra). Mantén un acorde **C5-E5-G5** y sube **Unison** de
   1 a 2, 4, 8 y 16 (Detune 25 %, Width 80 %): cada paso "engorda" el sonido, pero el volumen no sube. Con Unison 7,
   mueve **Detune** de 0 % a 10 %, 25 %, 50 % y 100 %: de un sonido limpio a un coro, a un supersaw y a algo desafinado.
4. **Ancho.** Unison 7, Detune 30 %. Mueve **Width** de 0 % a 100 % mirando el vectorscope: la línea vertical se abre en
   una nube. Con auriculares, el sonido pasa de estar "dentro de la cabeza" a rodearte. Mueve **Pan** de L 100 a R 100.
5. **Prueba de mono.** Con Width 100 %, gira la perilla de **separación estéreo** de la pista del Mixer (en el panel del
   insert) hasta el extremo "merge" (mono). El sonido se estrecha y pierde un poco de brillo y nivel: eso es lo que
   oirá alguien con un altavoz Bluetooth. Vuelve a dejarla en el centro.
6. **Sub.** Osc A: sierra, Unison 1. Enciende **Sub**: *Sine*, Octave **−1**, Level 75 %. Toca **A2 (55 Hz)** y **A3 (110 Hz)**
   apagando y encendiendo el sub: en el EQ aparece un pico grande en la fundamental. En altavoces pequeños casi no se oye;
   en auriculares o monitores buenos se **siente**. Prueba *Triangle* y *Square*: se oyen más en altavoces pequeños.
7. **Ruido.** Apaga Osc A y Sub. Enciende **Ruido**, Level 50 %. Mantén una nota y mueve **Color** de 0 % a 100 %: de
   viento/mar a "shhh" y a "tsss". Mira cómo la alfombra del EQ se inclina.
8. **Ruido solo en el ataque.** Enciende Osc A (sierra). Ruido: Level **0 %**, Color **70 %**. En la pestaña Modulación:
   Env 2 con Attack 1 ms, Decay **60 ms**, Sustain 0 %, y la ruta **Env 2 → Noise Level, +40 %**. Cada nota empieza con
   un "tsk" de ruido que desaparece enseguida: es un **transitorio** artificial, el truco de los plucks modernos.
9. **Artefactos a buscar:** clics al cambiar Unison, Width o Pan con un acorde sonando, al encender/apagar Osc B, el Sub
   o el Ruido (no debería haber ninguno); un leve deslizamiento (5 ms) al cambiar Octave con una nota sonando es normal.
   Con Unison 16 en los dos osciladores y acordes grandes, mira el medidor de CPU de FL (arriba): sube bastante.

### Recetas
**1. Supersaw de trance (lead o acordes, 138 BPM)**
1. Osc A: *Basic Shapes* **67 %** (sierra), Unison **7**, Detune **35 %**, Width **100 %**, Level 100 %.
2. Osc B: **On**, *Basic Shapes* **67 %**, Octave **+1**, Unison **5**, Detune **25 %**, Width **100 %**, Level **45 %**.
3. Voices **8**. Env 1: Attack **5 ms**, Decay 500 ms, Sustain **100 %**, Release **350 ms**.
4. Filtro **On**, **Low Pass 12 dB**, Cutoff **7 kHz**, Resonance 10 %: quita la "arena" de arriba sin apagarlo.
5. Master a **−6 dB** (dos osciladores con unison suman mucha energía).
6. Piano Roll: acordes **A4-C5-E5** / **F4-A4-C5** / **C5-E5-G5** / **G4-B4-D5**, un compás cada uno.
7. *Por qué funciona:* la sierra tiene todos los armónicos (brillo); las 7 copias con ±12 cents dan el "coro" del
   Roland JP-8000, el sinte que creó este sonido; la capa una octava arriba añade el brillo "que corta" y el Width lo
   abre a todo el panorama. El filtro suave quita la aspereza de los armónicos más altos.

**2. Reese bass (drum & bass, 174 BPM)**
1. Osc A: *Basic Shapes* **67 %** (sierra), Unison **1**.
2. Osc B: **On**, *Basic Shapes* **67 %**, Fine **+15 ct**, Unison **1**, Level 100 %. (Dos sierras casi iguales: los
   batidos lentos entre ellas son **el** sonido reese.)
3. Sub: **On**, *Sine*, Octave **−1**, Level **70 %**.
4. Voices **1**. Env 1: Attack 3 ms, Decay 500 ms, Sustain **100 %**, Release **80 ms**.
5. Filtro **On**, **Low Pass 24 dB**, Cutoff **350 Hz**, Resonance **20 %**, Drive **35 %**.
6. Modulación: LFO 1 *Triangle*, **Free**, **Sync**, Division **2 bars**; ruta **LFO 1 → Filter Cutoff, +12 %**
   (el filtro se abre y cierra despacio, ±1.2 octavas). Ruta 2: **LFO 1 → Osc B Pitch, 0.4 %** (el batido se acelera
   y se frena: el reese "se mueve").
7. Piano Roll: notas largas (1–2 compases) en **F3 (87 Hz)**, **E3 (82 Hz)** y **G3 (98 Hz)**.
8. *Por qué funciona:* el batido entre las dos sierras crea un movimiento que "rueda"; el sub mono y centrado sostiene
   la fundamental aunque el batido cancele los graves de las sierras por momentos, y el filtro con drive lo vuelve
   oscuro y agresivo. Width no importa aquí: sin unison, el bajo es mono (como debe ser un bajo en una pista de club).

**3. Pad con aire (ambient, lo-fi)**
1. Osc A: *Vowels*, Position **40 %**, Unison **5**, Detune **20 %**, Width **100 %**.
2. Osc B: **On**, *Harmonic Build*, Position **30 %**, Octave **−1**, Unison **3**, Detune **15 %**, Width **60 %**,
   Level **50 %**.
3. Ruido: **On**, Level **12 %**, Color **75 %** (un soplido fino, como el aire de una voz).
4. Voices **8**. Env 1: Attack **900 ms**, Decay 1 s, Sustain **100 %**, Release **2 s**.
5. Filtro **On**, **Low Pass 12 dB**, Cutoff **3 kHz**.
6. Modulación: LFO 1 *Sine*, Free, Sync apagado, Rate **0.1 Hz**; rutas **LFO 1 → Osc A Position, +25 %** y
   **LFO 1 → Osc A Detune, +10 %** (el coro se abre y se cierra con el cambio de vocal).
7. Piano Roll: acordes de 4 compases en la zona **C4–C6**.
8. *Por qué funciona:* dos tablas distintas a una octava de distancia dan un timbre que ninguna tiene sola; el unison
   abierto lo vuelve envolvente y el ruido fino le da la sensación de "aire" de un pad grabado. El LFO lento mueve la
   vocal y el ancho a la vez: el pad parece respirar.

### Reto sin receta
**El chord stab de future bass.** En el future bass (Flume, San Holo, Illenium) los acordes son golpes cortos,
enormes y muy anchos, que se abren en brillo al principio de cada golpe y tienen un "tsk" de aire en el ataque.
Tu reto, a **150 BPM**: un patrón de acordes en corcheas con síncopas (por ejemplo **F4-A4-C5-E5**), donde cada golpe:
- dure poco (se apague antes del siguiente) y suene **ancho** con auriculares,
- empiece brillante y se oscurezca enseguida,
- tenga un pequeño transitorio de ruido,
- y **siga sonando lleno** con la separación estéreo del Mixer en mono (no puede "desaparecer").

Inténtalo primero; las pistas vendrán después.

### Vocabulario
- **Capa (layer).** Varios osciladores sonando juntos como un solo sonido. *Ejemplo:* casi cualquier lead de EDM son
  2–3 capas (una sierra, una octava arriba y un sub).
- **Intervalo / quinta / octava.** La distancia entre dos notas; la quinta son 7 semitonos y la octava 12.
  *Ejemplo:* los "power chords" de guitarra (nota + quinta) en "Smells Like Teen Spirit".
- **Cent.** 1/100 de semitono. *Ejemplo:* los afinadores de guitarra muestran cuántos cents te faltan.
- **Batido (beating).** El pulso de volumen entre dos frecuencias cercanas. *Ejemplo:* dos cuerdas de guitarra casi
  afinadas: "uaa-uaa-uaa" antes de ajustar la clavija.
- **Detune.** Desafinar a propósito copias u osciladores. *Ejemplo:* el grosor de los sintes de "Blinding Lights"
  (The Weeknd).
- **Unison.** Varias copias desafinadas de la misma onda. *Ejemplo:* los leads de big room y trance ("Animals",
  Martin Garrix).
- **Supersaw.** Unison de sierras, originalmente del Roland JP-8000 (1996). *Ejemplo:* "Sandstorm" (Darude) y todo el
  trance de los 2000.
- **Ancho estéreo (width).** Cuánto se separan izquierda y derecha. *Ejemplo:* los pads que te "rodean" en los
  auriculares en la música ambient.
- **Paneo / ley de paneo.** Colocar un sonido en el panorama; "equal power" mantiene su volumen percibido.
  *Ejemplo:* la batería de los Beatles paneada a un lado en las mezclas estéreo de los 60.
- **Compatibilidad mono.** Que una mezcla siga sonando bien al sumar L + R. *Ejemplo:* un club o un altavoz
  Bluetooth: si el bajo es muy ancho, pierde pegada.
- **Sub / sub bass.** La parte más grave, que se siente más que se oye (≈ 30–80 Hz). *Ejemplo:* los 808 del trap.
- **Ruido blanco / color del ruido.** Todas las frecuencias al azar; el color decide si predominan graves o agudos.
  *Ejemplo:* los "risers" (subidas de ruido) antes de un drop.
- **Transitorio.** El inicio brevísimo y brillante de un sonido. *Ejemplo:* el "tick" de la púa en una guitarra o el
  "tsk" de ruido al principio de un pluck de future bass.
- **Reese.** Bajo de dos sierras ligeramente desafinadas. *Ejemplo:* el drum & bass de los 90 (el nombre viene de
  "Just Want Another Chance", de Reese / Kevin Saunderson).

### Retos completados
- [ ] Reto Fase 6 — chord stab de future bass

---

## Fase 7 — FM, ring mod y warp

### Conceptos aprendidos
- **FM (modulación de frecuencia).** Un oscilador (la **moduladora**) mueve muy rápido el tono de otro (la
  **portadora**). Con un LFO lento eso es un vibrato. Pero cuando la moduladora vibra a frecuencia de *audio*, el oído ya
  no oye un tono que sube y baja: oye un **timbre nuevo**. Aparecen **bandas laterales** alrededor de la portadora, en
  `portadora ± 1·moduladora`, `± 2·moduladora`, `± 3·moduladora`… Es la síntesis del Yamaha DX7 (1983): con dos senos
  se pueden hacer campanas, pianos eléctricos, metales o bajos.
- **En realidad es PM (modulación de fase).** Undertow no cambia la *velocidad* de la portadora: desplaza su *posición
  de lectura* (`leer(fase + índice × moduladora)`). Suena igual que la FM y es lo que hacía el DX7, pero tiene una
  ventaja: la nota nunca se desafina, porque el desplazamiento vuelve siempre a cero.
- **Índice de modulación (β).** Cuánto se mueve la portadora. Con β pequeño (< 1) solo aparecen 1 o 2 bandas laterales:
  brillo suave. Con β grande aparecen muchas: sonido metálico, agresivo. El nivel de cada banda lo dan las
  **funciones de Bessel** Jₙ(β). Los tests lo comprueban con un error de 0.11 dB. Un caso curioso: con β = 2.4 la
  portadora *desaparece* (J₀(2.4) = 0) y solo quedan las bandas laterales. La perilla FM Amount va en curva:

  | FM Amount | 10 % | 20 % | 28 % | 40 % | 50 % | 70 % | 100 % |
  |---|---|---|---|---|---|---|---|
  | β (índice) | 0.13 | 0.5 | 1.0 | 2.0 | 3.1 | 6.2 | 12.6 |

- **Ratio: armónico o inarmónico.** Lo que decide el *tipo* de timbre es la relación entre moduladora y portadora.
  - Si es un número entero (1:1, 2:1, 3:1…), todas las bandas laterales caen en múltiplos de una misma
    fundamental: el sonido es **armónico**, tiene tono claro (metales, órganos, bajos).
  - Si no es entero (1:1.41, 1:3.5…), las bandas caen "entre" los armónicos: el sonido es **inarmónico**, como una
    campana, un gong o un platillo.

  En Undertow la ratio se ajusta con **Octave / Semi / Fine del oscilador modulador**. Con Octave +1 la ratio es 2:1.
- **La moduladora no tiene por qué oírse.** On y Level deciden cuánto se *oye* un oscilador. Como moduladora se usa
  siempre su señal completa, aunque esté apagado. Así se hace la FM clásica: **Osc A suena, Osc B (apagado) lo modula.**
  También pueden modular el **sub** (FM grave, un "gruñido") y el **ruido** (FM "sucia", aire, arena).
- **Ring mod (RM).** Multiplica las dos señales. Resultado: la **suma y la diferencia** de sus frecuencias, y la
  original *desaparece*. Ejemplo: 440 Hz × 110 Hz = 330 Hz + 550 Hz. Casi siempre suena inarmónico, metálico, robótico
  (la voz de los Daleks de *Doctor Who*). Con el amount a medias es **AM** (modulación de amplitud): la original sigue
  sonando, con las dos bandas laterales a su lado.
- **Warp: deformar la fase antes de leer la tabla.** La tabla no cambia: cambia el *recorrido* por ella. Por eso
  cualquier warp funciona con cualquier wavetable. Con el amount a 0 la onda es exactamente la original.
  - **Sync (hard sync).** Hay un oscilador "esclavo" que corre más rápido que la nota y vuelve a empezar cada vez que
    el "maestro" (la nota) completa un ciclo. El tono sigue siendo el de la nota, pero el timbre tiene un **pico de
    resonancia** en la frecuencia del esclavo. Moverlo suena a un "uaaau" vocal y agresivo, el sonido de sync de los
    sintes analógicos. Cada 25 % de la perilla el esclavo sube una octava (×2, ×4, ×8, ×16).
  - **Bend + / Bend −.** Acelera unas partes del ciclo y frena otras. Bend + va rápido en los bordes y lento en el
    centro; Bend − al revés. Añade armónicos sin cambiar el tono. Suena más "apretado" o más "nasal", según la onda.
  - **PWM.** Comprime el ciclo entero en una parte y el resto se queda quieto. Con una cuadrada es la clásica
    modulación de ancho de pulso: más estrecho = más fino y "nasal". Con otras ondas crea formas nuevas.
  - **Mirror.** Mezcla la onda con su versión "ida y vuelta": el ciclo se lee al doble de velocidad hacia delante y
    luego hacia atrás. Resultado: una onda simétrica, más hueca, con carácter de órgano o de vocal.
  - **Quantize.** La onda avanza a saltos en el *tiempo*: solo 256 → 2 "muestras" por ciclo. Es el sonido de las
    wavetables de los sintes digitales de los 80 y de las consolas. Con 2 escalones, un seno se vuelve cuadrada.
  - **Bitcrush.** La onda avanza a saltos en la *amplitud*: de 8 bits a 1 bit. Suena "sucio", a videojuego. Con la
    perilla baja solo añade una arenilla fina.
- **Aliasing: cómo se controla aquí.** Deformar la fase *acelera* la lectura de la tabla. Por ejemplo, si una zona del
  ciclo se lee 8 veces más rápido, sus armónicos suben 8 veces y pueden pasar de Nyquist y reflejarse. Undertow usa
  tres técnicas a la vez:
  1. **Mipmap por velocidad.** Se elige el nivel de la tabla para la lectura *más rápida* (la FM incluida), no para
     la nota.
  2. **polyBLEP / polyBLAMP.** Los saltos (el reinicio del Sync, los escalones) y los quiebres (los bordes del PWM,
     la vuelta del Mirror) se suavizan justo en el instante en que ocurren, entre dos muestras.
  3. **Oversampling ×2.** En cuanto hay algún warp o FM/RM, las fuentes se calculan al doble de la frecuencia de
     muestreo. Un filtro *halfband* (100 dB de rechazo) quita todo lo que pasa de 20 kHz antes de volver a la
     frecuencia del host.

  Resultado medido (el peor caso, de C4 a C7): Sync −78 dB, PWM −80 dB, Mirror −92 dB, Quantize −82 dB, FM −72 dB.
  Un sync "ingenuo" sin nada de esto da −27 dB: un silbido claramente audible. Hay tres excepciones:
  - **Bend − al 100 %** llega a −62 dB: su curva frena y acelera muy bruscamente.
  - **Bitcrush de una sierra** llega a −53 dB: cerca del salto de la sierra, la onda cruza un escalón y vuelve
    dentro de una misma muestra.
  - En **C8** con amounts extremos la lectura pasa de 30 kHz y ya no se puede limitar del todo (−53 a −79 dB).
- **El precio: CPU.** Sin warp ni FM el sonido y el coste son los de la Fase 6. Con warp o FM, cada oscilador cuesta
  el doble (oversampling). La FM clásica cuesta el doble otra vez, porque el modulador también se calcula aunque no
  suene. Mide en tu PC: 8 notas con Sync ≈ 9 % de un núcleo; FM de 2 osciladores ≈ 14 %; Sync con 7 copias de
  unison ≈ 20 %.
- **Cambiar de modo no hace clic.** El modo de warp o de FM/RM cambia la onda de golpe, así que la voz baja el volumen
  en 2.5 ms, cambia el modo en silencio y vuelve a subir: en total, un hueco de ~6 ms que no se percibe como corte.
  Los *amounts* sí se pueden mover y modular libremente, sin huecos.

### Qué hace cada control al sonido
| Control | Qué se oye | En el analizador / osciloscopio |
|---|---|---|
| **Warp: Sync** | Un pico de "vocal" que sube con la perilla: "uaaau", agresivo, cortante. | Una montaña de armónicos que se desplaza hacia arriba; en el osciloscopio, varios ciclos rápidos "cortados" en cada ciclo de la nota. |
| **Warp: Bend + / −** | Más brillo y "tensión" sin cambiar el tono; + suena más apretado y − más nasal (depende de la onda). | Aparecen armónicos nuevos; la forma se "inclina" hacia los bordes o hacia el centro. |
| **Warp: PWM** | La onda se vuelve más fina y hueca a medida que se estrecha; con LFO, el clásico "coro" de los pads analógicos. | Un pulso estrecho seguido de una zona plana; el espectro se llena de armónicos. |
| **Warp: Mirror** | Más hueco y simétrico, a órgano o a vocal "o". | La forma queda simétrica (la segunda mitad es la primera al revés) y cambia el reparto de armónicos. |
| **Warp: Quantize** | De casi nada (arriba) a "8 bits" y a cuadrada (al 100 %). | Una escalera en el tiempo; muchos armónicos agudos nuevos. |
| **Warp: Bitcrush** | Arenilla fina → suciedad de videojuego → distorsión dura. | Una escalera en la amplitud. |
| **FM: Osc B / Sub / Noise** | Poco: más brillo, "vida". Medio: metal, campana (según la ratio). Mucho: ruido metálico, agresivo. Noise: arena y aire. Sub: gruñido grave. | Bandas laterales a distancias iguales alrededor de cada armónico; con ratio no entero, líneas "entre" los armónicos. |
| **RM: Osc B / Sub / Noise** | Al 100 %: robótico, metálico, sin la nota original. Al 50 %: la nota con un halo metálico (AM). Noise: ruido con el ritmo de la nota. | La línea de la nota se parte en dos (suma y diferencia). |
| **Destinos nuevos** | Env → Osc A Warp: sync que "se cierra" en cada nota. LFO → Osc A FM/RM: el metal "respira". Mod Wheel → Warp: control en vivo. | — |

### Ejercicio de escucha guiado
Montaje igual que en la Fase 6: en el canal del Mixer de Undertow, **Fruity Parametric EQ 2** (espectro) y
**Wave Candy** con un preset de **osciloscopio**. Todo lo nuevo está en la pestaña **Warp y FM**. El dibujo de cada
oscilador muestra la onda original (gris) y la deformada (naranja): mírala mientras mueves las perillas.

1. **Sync.** Osc A: *Basic Shapes* **67 %** (sierra). Warp **Sync**, Amount **0 %**. Mantén **C4 (131 Hz)** y sube
   Amount despacio hasta 100 %: el tono no cambia, pero un pico de timbre sube como una vocal ("uaaau"). En el EQ la
   montaña de armónicos se desplaza. Fíjate en 25 %, 50 % y 75 %: el esclavo está en ×2, ×4 y ×8, el sonido se
   "limpia" un instante y suena como una octava. Prueba lo mismo con el seno (Position 0 %).
2. **Sync con envolvente.** Amount a **20 %**. Pestaña Modulación: Env 2 con Attack 1 ms, Decay **400 ms**, Sustain 0 %,
   y ruta **Env 2 → Osc A Warp, +50 %**. Toca notas cortas: cada una empieza muy brillante y se "cierra". Es el
   *sync lead* de los 80.
3. **FM básica.** Quita la ruta. Osc A: *Basic Shapes* **0 %** (seno), Warp None. Osc B: *Basic Shapes* 0 %, **apagado**.
   En Osc A, FM/RM: **FM: Osc B**. Mantén **A5 (440 Hz)** y sube FM Amount de 0 a **30 %**: el seno gana brillo, suena
   más "vivo". Sigue a **50 %** (metálico) y **80 %** (agresivo). En el EQ aparecen líneas a 880, 1320, 1760 Hz…
4. **Ratio.** FM Amount **40 %**. En la pestaña Osciladores cambia **Osc B**:
   - **Octave +1** (ratio 2:1): sonido hueco, tipo clarinete.
   - **Octave −1**: se oye una octava más grave y con cuerpo.
   - Luego vuelve a Octave 0 y pon **Fine +30 ct**: el sonido se vuelve inarmónico, "desafinado", como una campana
     rota. Cada ratio es un instrumento distinto.
5. **Ring mod.** Osc B: Octave 0, **Semi +5**, Fine 0. En Osc A cambia a **RM: Osc B**, Amount **100 %**. Toca una
   melodía: suena robótico y metálico, y la nota original ya no está. Baja a **50 %**: vuelve la nota, con un halo
   metálico (AM).
6. **Sub y ruido como moduladores.** Osc A (seno): **FM: Sub**, Amount 40 %. Deja el Sub **apagado**: sigue modulando.
   Con Octave del sub en −1 se oye un gruñido grave. Luego **FM: Noise**, Amount **15 %**: el seno se vuelve "arenoso",
   como una flauta con aire.
7. **Los otros warps.** Osc A: *Basic Shapes* **100 %** (cuadrada), FM Off. Prueba:
   - **PWM** (0 → 90 %: de cuadrada a pulso fino).
   - **Mirror** (0 → 100 %).
   - **Bend +** y **Bend −**.
   - **Quantize** (100 → 50 → 0 %).
   - **Bitcrush** (0 → 60 → 100 %).

   Mira el dibujo naranja y el osciloscopio a la vez: son la misma forma.
8. **Artefactos a buscar.**
   - Cambiar el modo de Warp o de FM/RM con una nota sonando: se oye un micro-hueco de ~6 ms, pero **ningún clic**.
   - Mover los Amounts, o modularlos con un LFO rápido: sin clics ni "zipper".
   - Con un modo elegido y el Amount en 0 %, el sonido debe ser **idéntico** al de None/Off.
   - En notas muy agudas (C7–C8) con Sync o Bend al máximo, escucha con atención por si aparece un "silbido" que no
     sigue a la nota. Debería estar muy por debajo de lo audible.
   - Sube el medidor de CPU de FL con acordes grandes y unison: el warp y la FM cuestan más.

### Recetas
**1. Sync lead de los 80 (synth-pop, 120 BPM)**
1. Osc A: *Basic Shapes* **67 %** (sierra), Unison **3**, Detune **12 %**. Warp **Sync**, Amount **25 %**.
2. Osc B: **On**, *Basic Shapes* **67 %**, Octave **−1**, Level **40 %** (cuerpo debajo del sync).
3. Voices **1**. Env 1: Attack 3 ms, Decay 500 ms, Sustain **80 %**, Release 200 ms.
4. Env 2: Attack 1 ms, Decay **350 ms**, Sustain **15 %**, Release 200 ms; ruta **Env 2 → Osc A Warp, +45 %**.
5. LFO 1: *Sine*, Retrigger, Sync, Division **1/8**; ruta **LFO 1 → Osc A Warp, +5 %** (un leve "wah" rítmico).
6. Filtro **On**, **Low Pass 12 dB**, Cutoff **6 kHz**, Resonance 10 %.
7. Piano Roll: una melodía en corcheas entre **C5 y C6**.
8. *Por qué funciona:*
   - El sync añade un pico de timbre que la envolvente barre de arriba abajo en cada nota. El ataque suena brillante
     y agresivo, y el resto de la nota, más redondo.
   - El sync se queda en ratios no enteros casi todo el tiempo: ahí es donde tiene "mordida".
   - La capa una octava abajo da cuerpo y el unison suave, anchura sin perder claridad.

   Es el sonido de "Let's Go" (The Cars) y de muchos leads de los 80.

**2. Campana FM (ambient, cine)**
1. Osc A: *Basic Shapes* **0 %** (seno). FM/RM: **FM: Osc B**, Amount **35 %**.
2. Osc B: **apagado**, *Basic Shapes* 0 %, Octave **+1**, Semi **+10** (ratio ≈ 3.56: inarmónico, "de metal").
3. Voices **8**. Env 1: Attack **1 ms**, Decay **3 s**, Sustain **0 %**, Release **2.5 s**.
4. Env 2: Attack 1 ms, Decay **1.2 s**, Sustain 0 %; ruta **Env 2 → Osc A FM/RM, +30 %**.
5. Ruta 2: **Velocity → Osc A FM/RM, +15 %** (tocar fuerte = golpe más metálico).
6. Piano Roll: notas sueltas entre **C5 y C7**, dejando espacio para que suenen.
7. *Por qué funciona:*
   - Una campana real tiene parciales inarmónicos: la ratio no entera los crea.
   - Al golpear, una campana es muy brillante y los parciales agudos mueren primero: Env 2 baja el índice de FM
     mientras la nota decae, y el sonido pasa de metálico a un tono casi puro.
   - Velocity → FM imita cómo un golpe fuerte excita más armónicos.

**3. Bajo FM con gruñido (dubstep / bass music, 140 BPM)**
1. Osc A: *Basic Shapes* **0 %** (seno). FM/RM: **FM: Osc B**, Amount **30 %**.
2. Osc B: **apagado**, *Basic Shapes* **67 %** (sierra: modular con una sierra da más armónicos), Octave **0**.
3. Sub: **On**, *Sine*, Octave **−1**, Level **70 %** (la base limpia que no se mueve).
4. Voices **1**. Env 1: Attack 2 ms, Decay 300 ms, Sustain **100 %**, Release 80 ms.
5. LFO 1: *Sine*, Retrigger, **Sync**, Division **1/8**; ruta **LFO 1 → Osc A FM/RM, +25 %**.
6. Filtro **On**, **Low Pass 24 dB**, Cutoff **1.5 kHz**, Resonance **30 %**, Drive **40 %**. Ruta 2:
   **LFO 1 → Filter Cutoff, +15 %**.
7. Piano Roll: notas largas en **F2 (43.7 Hz)** y **G2 (49 Hz)**, con alguna nota a la octava.
8. *Por qué funciona:*
   - La FM a ratio 1:1 mantiene el tono claro, y el LFO abre y cierra el índice a ritmo de corchea: el bajo "habla".
   - El filtro con resonancia se mueve a la vez y acentúa ese movimiento, como una vocal.
   - El sub mono sostiene la fundamental aunque la FM la debilite en algunos momentos (recuerda J₀ = 0 con β = 2.4).

### Reto sin receta
**El "growl" de riddim / dubstep.** En el dubstep moderno (riddim, tearout) los bajos "hablan": parecen decir
"yoy-yoy" o "wow", y a la vez tienen un grave limpio y enorme.

Tu reto, a **140 BPM**: un bajo en **F2 (43.7 Hz)** con un patrón de 1 compás donde:
- el timbre cambie claramente de "vocal" varias veces (al menos 2 "sílabas" distintas por compás),
- el grave no desaparezca ni tiemble cuando el timbre se mueve,
- suene agresivo pero **sin clics** en ninguna nota,
- y funcione en mono (el grave no puede depender del estéreo).

Pista para empezar: esta fase te da tres formas de mover la "vocal": el Sync, la FM y el Position de *Vowels*.
Inténtalo primero; las pistas vendrán después.

### Vocabulario
- **FM / PM (modulación de frecuencia / de fase).** Un oscilador mueve el tono (o la fase) de otro a velocidad de
  audio. *Ejemplo:* el piano eléctrico del Yamaha DX7 (preset "E.PIANO 1"), omnipresente en las baladas de los 80.
- **Portadora / moduladora.** La que se oye / la que la transforma. *Ejemplo:* en la campana de la receta 2, A es la
  portadora y B (apagado) la moduladora.
- **Ratio.** La relación de frecuencias entre moduladora y portadora; entera = armónico, no entera = inarmónico.
  *Ejemplo:* los pianos eléctricos FM usan ratios enteras; las campanas y gongs, no enteras.
- **Índice de modulación.** Cuánto modula la moduladora: más índice = más bandas laterales = más brillo y metal.
  *Ejemplo:* el "golpe" brillante de un bajo FM de synth-pop que se apaga enseguida.
- **Bandas laterales.** Las frecuencias nuevas que aparecen alrededor de la portadora. *Ejemplo:* el halo metálico de
  un sonido de campana FM.
- **Inarmónico.** Un sonido cuyos parciales no son múltiplos de una fundamental. *Ejemplo:* campanas de iglesia,
  platillos, gongs.
- **Ring mod.** Multiplicar dos señales: suma y diferencia de frecuencias. *Ejemplo:* la voz de los Daleks en
  *Doctor Who*.
- **AM (modulación de amplitud).** Mover el volumen a velocidad de audio. *Ejemplo:* el trémolo muy rápido de algunos
  pedales de guitarra se vuelve un timbre "metálico" cuando pasa de ~20 Hz.
- **Hard sync / maestro y esclavo.** Un oscilador que se reinicia con el ciclo de otro. *Ejemplo:* el lead de
  "Let's Go" (The Cars, 1979).
- **Warp / phase distortion.** Deformar la fase de lectura para cambiar el timbre. *Ejemplo:* los sintes Casio CZ de
  los 80 hacían todo su sonido así.
- **PWM (modulación de ancho de pulso).** Cambiar lo ancho del pulso de una onda cuadrada. *Ejemplo:* los pads y
  cuerdas del Roland Juno-106.
- **Bitcrush / profundidad de bits.** Reducir la resolución de amplitud. *Ejemplo:* la música de videojuegos de 8 bits
  (chiptune).
- **Oversampling.** Calcular a una frecuencia de muestreo más alta para que los armónicos tengan sitio antes de
  reflejarse. *Ejemplo:* el botón "HQ" u "oversampling" de muchos plugins de distorsión.
- **polyBLEP.** Una corrección pequeña alrededor de cada salto de la onda que la vuelve limitada en banda. *Ejemplo:*
  la usan muchos osciladores "analógicos virtuales" para que una sierra aguda no silbe.

### Retos completados
- [ ] Reto Fase 7 — growl de riddim / dubstep

---

## Fase 8 — Efectos: distorsión, chorus, delay y reverb

### Conceptos aprendidos
**Efectos globales vs. por voz.** Todo lo anterior (osciladores, filtro, envolventes) existe **una vez por nota**. Los
efectos procesan la **suma** de todas las notas, una sola vez. Por eso cuestan poco CPU, pero también por eso se
comportan distinto: distorsionar un acorde entero no suena igual que distorsionar cada nota por separado (las notas
se "mezclan" dentro de la distorsión y aparecen frecuencias nuevas, suma y diferencia de ellas: *intermodulación*).
El orden es fijo, el clásico: **distorsión → chorus → delay → reverb**. Si la reverb fuera primero, la distorsión
convertiría su cola en un zumbido sin forma.

**Distorsión = waveshaping.** Se pasa la señal por una curva que no es una recta. Si la curva "aplasta" los picos, un
seno se vuelve más cuadrado, y lo cuadrado son armónicos nuevos: brillo y agresividad.
- **Curvas simétricas** (que tratan igual la mitad positiva y la negativa: Soft Clip, Hard Clip, Fold) solo crean
  armónicos **impares** (3.º, 5.º, 7.º…): sonido hueco y "de radio".
- **Curvas asimétricas** (Tube) crean además armónicos **pares** (2.º, 4.º…), que son octavas y quintas de la nota:
  suenan más "gordas" y musicales. Es parte de por qué la gente describe las válvulas como "cálidas".
- **Fold** (plegado) no aplasta la onda: cuando pasa del límite la dobla hacia dentro. Cada pliegue añade armónicos,
  como la FM. Es el timbre metálico y "hablador" de la síntesis west coast (Buchla).

El problema digital es el mismo de la Fase 3: esos armónicos no tienen fin, y lo que pasa de la mitad de la frecuencia
de muestreo se refleja como **aliasing**. Aquí se combate con dos armas. Primero, **oversampling ×4**: la distorsión
ocurre a 4 veces la frecuencia del host y después se filtra. Segundo, **ADAA**: en lugar de evaluar la curva en cada
muestra, se promedia la curva entre la muestra anterior y la actual. Ese promedio redondea las esquinas que crean los
armónicos más agudos. Medido: un seno de 440 Hz con el drive al máximo deja el alias a −95 dB; sin estas dos técnicas,
a −17 dB (se oye como un "pitido" desafinado encima de la nota).

**Compensación de volumen.** Subir el drive también sube el volumen, y el oído engaña: "más fuerte" parece "mejor".
El plugin compensa automáticamente para que una señal de nivel normal salga con el mismo pico. Así, al comparar drive
0 % y 80 %, oyes el cambio de **timbre**, no de volumen.

**Chorus.** Es una copia de la señal retrasada unos 10 ms, con ese retraso moviéndose lentamente con un LFO. Mientras
el retraso crece, la copia suena un pelo más grave, y mientras decrece, un pelo más aguda (el efecto Doppler de una
sirena que pasa). Mezclada con la original da la sensación de "varios instrumentos". En este chorus la copia de la
derecha se mueve al revés que la de la izquierda (como en el Roland Juno): el sonido se abre en estéreo. Con
**feedback**, la copia vuelve a entrar y se acerca a un **flanger** (el "avión" metálico).

**Delay (eco).** Una memoria de hasta 4 s: lo que entra sale X tiempo después. Con **feedback**, la salida vuelve a
entrar: cada eco es el anterior multiplicado por el feedback. Con 50 %, cada eco está 6 dB por debajo. **Tone** es un
low-pass dentro del lazo: cada repetición pasa otra vez por él y sale más oscura, como en un delay de cinta. Con
**Sync**, el tiempo se mide en figuras musicales y se ajusta solo si cambias el tempo en FL.

**Reverb.** Una sala real devuelve miles de reflexiones, cada vez más juntas y más débiles. La reverb de Undertow las
fabrica con una **red de 8 líneas de retardo realimentadas** (FDN). Lo que sale de cada línea se reparte entre todas
las demás, así que el número de ecos se multiplica en cada vuelta, como en una sala. Antes de la red hay **difusores**
(filtros all-pass) que convierten el golpe de entrada en una nube densa desde el principio. El **Decay** es el RT60 de
la acústica: el tiempo que tarda el sonido en caer 60 dB, y se cumple (medido: −5 %). El **Pre-Delay** es el hueco
entre el sonido directo y la reverb: el cerebro lo usa para "medir" la sala.

### Qué hace cada control al sonido
| Control | Qué se oye | Qué se ve (espectro / osciloscopio) |
|---|---|---|
| **Distorsión: Soft Clip** | Poco drive: calor, "cuerpo", más presencia. Mucho: fuzz redondo. | Los picos se redondean; aparecen el 3.º, 5.º… armónicos. |
| **Hard Clip** | Más agresivo y "digital" que el Soft; con mucho drive, casi una cuadrada. | Picos cortados en plano; armónicos impares que bajan despacio. |
| **Tube** | Más gordo y cálido; en acordes, más "sucio". | Onda asimétrica; aparecen el 2.º y el 4.º armónico. |
| **Fold** | Metálico, vocal, cambia muchísimo con el drive: un barrido de drive "habla". | La onda se dobla hacia dentro; el espectro se llena de armónicos que suben y bajan. |
| **Drive** | Cuánta distorsión. El volumen se mantiene: lo que cambia es el brillo y la agresividad. | Más armónicos, más altos. |
| **Tone** | Oscurece solo lo distorsionado: quita el "fizz" (el siseo áspero de arriba). | Corta los armónicos altos que acaba de crear la distorsión. |
| **Mix (distorsión)** | Mezcla en paralelo: conserva el ataque y el grave limpios con el "pelo" de la distorsión encima. | — |
| **Chorus: Rate** | Lento (0.2–1 Hz): ondulación suave. Rápido (3–8 Hz): vibrato, mareo. | — |
| **Chorus: Depth** | Cuánto se desafina la copia. Poco: ancho sutil. Mucho: "desafinado a propósito" (lo-fi, cinta vieja). | — |
| **Chorus: Feedback** | Hacia el flanger: un silbido metálico que sube y baja. | Picos y valles (un "peine") que se mueven por el espectro. |
| **Chorus: Mix** | 50 % = el chorus clásico (la cancelación entre copia y original es lo que "mueve"). 100 % = vibrato puro. | — |
| **Delay: Time / Division** | Corto (< 50 ms): "doblaje", slapback. 1/8 o 1/4: eco rítmico. 1/8 D: el "galope" de U2 / The Edge. | En el osciloscopio: copias de la nota separadas por el tiempo del eco. |
| **Delay: Feedback** | Cuántas repeticiones. > 80 %: los ecos se acumulan y "lavan" la mezcla. | — |
| **Delay: Tone** | Ecos oscuros = se quedan "detrás" y no molestan a la voz principal. | Cada eco con menos agudos que el anterior. |
| **Ping-Pong** | Los ecos saltan de izquierda a derecha: ancho y movimiento. | — |
| **Reverb: Size** | Pequeño: habitación, ecos densos y cortos. Grande: catedral, ecos más separados. | — |
| **Reverb: Decay** | Cuánto dura la cola: 0.5 s habitación, 2–3 s sala, 8–30 s espacio "infinito" (ambient). | La cola en el osciloscopio. |
| **Reverb: Damping** | Alto: cola oscura y suave (sala con cortinas). Bajo: cola brillante y metálica (baño de azulejos). | Los agudos de la cola caen antes que los graves. |
| **Reverb: Pre-Delay** | 0: el sonido "dentro" de la reverb. 30–80 ms: el ataque queda limpio delante y la reverb detrás. | — |
| **Reverb: Mix** | 10–20 %: espacio natural. 50 %+: sonido lejano, de "fondo". | — |

### Ejercicio de escucha guiado
Montaje igual que en las fases anteriores: en el canal del Mixer de Undertow, **Fruity Parametric EQ 2** (espectro) y
**Wave Candy** (osciloscopio). Todo lo nuevo está en la pestaña **Efectos**.

1. **Distorsión y armónicos.** Osc A: *Basic Shapes* **0 %** (seno). Mantén **A5 (440 Hz)**. Distorsión **On**, Soft
   Clip, Tone 100 %, Mix 100 %. Sube Drive de 0 a 100 %. En el EQ aparecen líneas en 1320, 2200, 3080 Hz (impares), pero
   **no** en 880 ni 1760. Cambia a **Tube**: ahora sí aparece 880 Hz (el 2.º armónico). Fíjate en que el volumen
   percibido casi no cambia.
2. **Hard Clip vs Soft Clip.** Misma nota, Drive **70 %**. Alterna Soft ↔ Hard: el Hard es más áspero. En el
   osciloscopio, el Soft tiene los hombros redondos y el Hard, planos y cortados.
3. **Fold.** Modo **Fold**, Drive **0 %** y súbelo muy despacio hasta 100 %: el timbre "habla", como una vocal que
   cambia. Es un barrido muy distinto al del filtro. Pon una ruta de modulación a algo que ya conoces… ¡no se puede! Los
   efectos no son destinos de la matriz (son globales). Por ahora, automatízalo en FL (clic derecho → *Create
   automation clip*).
4. **Tone y Mix.** Cambia a *Basic Shapes* **67 %** (sierra), nota **C4 (131 Hz)**, Soft Clip Drive **80 %**: mucho
   "fizz" arriba. Baja Tone a **40 %**: el fizz desaparece y queda el cuerpo. Ahora Mix **40 %**: vuelve el ataque limpio
   con la distorsión "por detrás" (distorsión en paralelo, un truco clásico de mezcla).
5. **Chorus.** Apaga la distorsión. Osc A sierra, unison 1. Toca un acorde **C5–E5–G5** y enciende el **Chorus**
   (Rate 0.8 Hz, Depth 50 %, Mix 50 %). Con auriculares: el acorde se abre a los lados y "respira". Sube Depth a 100 % y
   Rate a 5 Hz: mareo. Vuelve a Rate 0.3 Hz y sube Feedback a 80 %: flanger.
6. **Delay.** Apaga el chorus. Nota corta (Attack 1 ms, Decay 200 ms, Sustain 0 %, Release 100 ms). Delay **On**,
   Sync, **1/4**, Feedback 40 %, Mix 30 %. Pon un patrón de negras en el Piano Roll a 120 BPM: los ecos caen justo encima
   de las notas. Cambia a **1/8 D**: los ecos caen "entre" las notas y crean un ritmo nuevo. Activa **Ping-Pong** y
   escucha con auriculares. Baja Tone a 20 %: los ecos se oscurecen y se van "atrás".
7. **Reverb.** Apaga el delay. Notas cortas sueltas (tipo pluck). Reverb **On**, Size 50 %, Decay 2.5 s, Mix 30 %.
   Prueba Decay 0.6 s (habitación) → 8 s (catedral). Damping 0 % vs 90 %: cola brillante vs oscura. Pre-Delay 0 vs
   **60 ms**: con 60 ms, el golpe de la nota se oye nítido y la reverb llega un instante después.
8. **Artefactos a buscar.**
   - Encender y apagar cada efecto con notas sonando: **sin clics**. Al apagar el delay o la reverb, la cola se corta
     en 10 ms (a propósito).
   - Cambiar la división del delay con ecos sonando: un fundido de 50 ms, **sin** que los ecos se desafinen.
   - Activar o desactivar Ping-Pong con ecos: sin clic.
   - Con todos los efectos apagados, el sonido debe ser **idéntico** al de la Fase 7.
   - Con la distorsión al máximo en notas agudas (C7–C8), escucha si hay un pitido que no sigue a la nota: debería estar
     muy por debajo de la nota (medido: −55 dB en C8).
   - Colas largas (Decay 20 s + delay con feedback 90 %) durante un rato: el volumen no debe crecer sin control.
   - Al exportar desde FL, la cola de la reverb y del delay debe quedar completa (el plugin informa cuánto dura).

### Recetas
**1. Pad "Juno" de los 80 (synthwave, 100 BPM)**
1. Osc A: *Basic Shapes* **67 %** (sierra), Unison **1**. Osc B: **On**, *Basic Shapes* **100 %** (cuadrada),
   Octave **−1**, Level **50 %**.
2. Filtro **On**, **Low Pass 24 dB**, Cutoff **2.2 kHz**, Resonance **15 %**, Key Track **40 %**.
3. Env 1: Attack **400 ms**, Decay 1 s, Sustain **80 %**, Release **1.5 s**. Voices **8**.
4. LFO 1: *Triangle*, Free, sin Sync, Rate **0.2 Hz**; ruta **LFO 1 → Filter Cutoff, +6 %** (una respiración lenta).
5. **Chorus** On: Rate **0.5 Hz**, Depth **60 %**, Feedback 0 %, Mix **50 %**.
6. **Reverb** On: Size **70 %**, Decay **4 s**, Damping **50 %**, Pre-Delay **20 ms**, Mix **30 %**.
7. Piano Roll: acordes largos de 2 compases (**Am – F – C – G**, en la octava 4–5).
8. *Por qué funciona:*
   - Una sola sierra con **chorus** suena más "analógica" y ancha que un unison: el chorus se mueve lento y de forma
     continua, justo como el Juno-60 (ese chorus ES el sonido de ese sinte).
   - El filtro con key tracking evita que las notas altas chillen.
   - La reverb larga pero oscura (damping) crea espacio sin tapar el acorde siguiente.

**2. Bajo medio distorsionado (drum & bass / house, 174 o 124 BPM)**
1. Osc A: *Basic Shapes* **67 %** (sierra), Unison **2**, Detune **10 %**, Width **0 %** (mono: el bajo debe ir al centro).
2. Sub: **On**, *Sine*, Octave **−1**, Level **60 %**.
3. Filtro **On**, **Low Pass 24 dB**, Cutoff **600 Hz**, Resonance **20 %**. Env 2: Attack 1 ms, Decay **250 ms**,
   Sustain **20 %**; ruta **Env 2 → Filter Cutoff, +30 %**.
4. Voices **1**. Env 1: Attack 2 ms, Decay 400 ms, Sustain **90 %**, Release 60 ms.
5. **Distorsión** On: **Tube**, Drive **55 %**, Tone **45 %**, Mix **70 %**.
6. Sin chorus, delay ni reverb (el bajo seco y centrado).
7. Piano Roll: un patrón de corcheas con notas en **E3 (82 Hz)** y **G3 (98 Hz)**, alguna nota a la octava.
8. *Por qué funciona:*
   - La distorsión **después** del filtro añade armónicos nuevos arriba: el bajo se oye en altavoces pequeños (en el
     móvil no suena el grave, pero sí sus armónicos, y el cerebro "reconstruye" la nota).
   - Tube añade el 2.º armónico: más gordura que un clip simétrico.
   - Tone recorta el fizz y el Mix al 70 % conserva el golpe limpio del ataque y el sub intacto.

**3. Lead con eco "galopante" (pop / trance)**
1. Osc A: *Basic Shapes* **67 %**, Unison **3**, Detune **15 %**. Voices **1**.
2. Filtro Low Pass 12 dB, Cutoff **5 kHz**. Env 1: Attack 3 ms, Decay 300 ms, Sustain 70 %, Release 150 ms.
3. **Delay** On: Sync, **1/8 D**, Feedback **35 %**, **Ping-Pong**, Tone **40 %**, Mix **25 %**.
4. **Reverb** On: Size 50 %, Decay **1.8 s**, Damping 40 %, Pre-Delay **40 ms**, Mix **18 %**.
5. Piano Roll: una melodía en negras y corcheas entre **C6 y C7**.
6. *Por qué funciona:* el eco a 1/8 con puntillo cae en los huecos de la melodía y "rellena" el ritmo sin pisar las
   notas; oscuro y rebotando a los lados, no compite con el lead del centro. El pre-delay deja el ataque nítido.

### Reto sin receta
**Un lead de synthwave** (referencias: Kavinsky – "Nightcall", The Midnight, la banda sonora de *Drive*).

Tu reto, a **100 BPM**: una melodía de 2 compases entre **C5 y C6** donde:
- el lead suene "de los 80": ancho, brillante pero no áspero, con un leve vaivén,
- los ecos marquen el ritmo sin ensuciar la melodía,
- se sienta en un espacio grande pero se entienda cada nota,
- y, en mono (pon el Mixer de FL en mono un momento), no pierda cuerpo.

Pista para empezar: los tres efectos "de espacio" de esta fase hacen tres trabajos distintos (ancho, ritmo y sala).
Inténtalo primero; las pistas vendrán después.

### Vocabulario
- **Saturación / clipping.** Aplastar (suave) o cortar (duro) los picos de la onda. *Ejemplo:* el "calor" de una
  cinta analógica empujada; el fuzz de "Satisfaction" (The Rolling Stones).
- **Armónicos pares e impares.** Pares = octavas y quintas (cálidos, "gordos"); impares = sonido hueco, de clarinete o
  de radio. *Ejemplo:* un amplificador de válvulas saturado suave (pares) frente a un fuzz de transistores (impares).
- **Waveshaping.** Transformar la onda con una curva. *Ejemplo:* casi toda distorsión de guitarra y de sinte.
- **Wavefolding.** Plegar la onda sobre sí misma cuando pasa de un límite. *Ejemplo:* los sintes Buchla y los módulos
  "west coast"; muchos bajos de neurofunk.
- **Intermodulación.** Frecuencias nuevas (sumas y diferencias) al distorsionar varias notas juntas. *Ejemplo:* por eso
  un power chord (solo tónica y quinta) suena bien distorsionado y un acorde mayor completo suena "sucio".
- **Distorsión en paralelo.** Mezclar la señal distorsionada con la limpia (Mix). *Ejemplo:* la compresión y
  distorsión en paralelo de las baterías y bajos del rock y la música electrónica.
- **Chorus.** Copias levemente desafinadas y en movimiento. *Ejemplo:* las guitarras limpias de "Come As You Are"
  (Nirvana); los pads del Juno-60.
- **Flanger.** Chorus muy corto con realimentación: silbido de avión. *Ejemplo:* "Itchycoo Park" (Small Faces, 1967), uno de los
  primeros flangers famosos.
- **Efecto Doppler.** El tono sube cuando la fuente se acerca y baja cuando se aleja; un retardo que cambia hace lo
  mismo. *Ejemplo:* una ambulancia que pasa; un altavoz Leslie de órgano.
- **Delay / eco, feedback.** Repetición retrasada; feedback = cuántas repeticiones. *Ejemplo:* casi cualquier dub jamaicano
  (King Tubby, Lee "Scratch" Perry), donde el eco es un instrumento más.
- **Slapback.** Un solo eco corto (80–150 ms). *Ejemplo:* la voz de Elvis en sus grabaciones de Sun Records
  ("That's All Right", 1954).
- **Puntillo / tresillo (dotted / triplet).** Figuras de 1.5× y 2/3 de la duración. *Ejemplo:* el delay a 1/8 con
  puntillo de The Edge en "Where the Streets Have No Name" (U2).
- **Ping-pong.** Ecos que alternan entre izquierda y derecha. *Ejemplo:* muchas producciones de dub y de trance.
- **Reverb, cola (tail).** La persistencia del sonido en un espacio; la cola es lo que queda al soltar. *Ejemplo:* la
  batería con reverb gigante de "In the Air Tonight" (Phil Collins).
- **RT60 / Decay.** Tiempo que tarda la reverb en caer 60 dB. *Ejemplo:* una habitación ≈ 0.5 s; una catedral, 5–10 s.
- **Pre-delay.** Hueco entre el sonido directo y la reverb. *Ejemplo:* las voces de pop llevan 20–80 ms para que la
  letra se entienda aunque tengan mucha reverb.
- **Difusión, reflexiones tempranas.** Los primeros ecos, que dan la sensación del tamaño y la forma de la sala.
  *Ejemplo:* la diferencia entre aplaudir en un pasillo (ecos sueltos, "flutter") y en un auditorio (nube densa).
- **Damping.** Cuánto se apagan los agudos en la cola. *Ejemplo:* un estudio con paredes de tela (mucho damping)
  frente a un baño de azulejos (poco).
- **Dry / wet.** Señal original / procesada; el Mix las reparte. *Ejemplo:* "reverb al 20 %" = 80 % seco, 20 % mojado.

### Retos completados
- [ ] Reto Fase 8 — lead de synthwave
