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
