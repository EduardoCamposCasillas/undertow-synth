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
