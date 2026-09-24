# Cuaderno de diseño sonoro — Undertow Synth

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
- **Nota.** Cambia la altura. Por debajo de ~Do2 (65 Hz) el seno se *siente* más de lo que se *oye*.
  En altavoces de portátil prácticamente desaparece, porque no tiene armónicos que "delaten" la nota.
- **Velocity.** Solo cambia el volumen; el timbre no cambia. En un seno puro no existe
  "más fuerte = más brillante". Eso llegará con filtros y modulación.
- **En el analizador de espectro** se ve **una sola línea**: la fundamental. No hay armónicos.
- **En el osciloscopio** se ve una curva suave y perfectamente redonda.

### Ejercicio de escucha guiado
1. En FL, pon Undertow Synth en el Channel Rack. En su canal del Mixer inserta **Fruity Parametric EQ 2**
   (lo usarás como analizador) y **Wave Candy** en modo osciloscopio.
2. Toca un **La4 (A4)**. En el EQ verás un pico en 440 Hz y en Wave Candy un seno limpio.
3. Toca **A3** y luego **A5**. El pico salta a 220 Hz y a 880 Hz: cada octava duplica la frecuencia.
   En el osciloscopio caben el doble (o la mitad) de ciclos.
4. Baja nota a nota desde **C3** hasta **C1** y escucha **dónde "se pierde"** en tus altavoces
   y dónde en audífonos. Esa diferencia es el problema clásico del sub bass.
5. Toca notas cortas y staccato con volumen alto. Busca clics: **no debería haber ninguno**.
6. Toca legato: pulsa C3, sin soltarla pulsa E3 y luego suelta C3. E3 debe seguir sonando sin cortes
   (prioridad a la última nota).

### Recetas
**Sub bass limpio (estilo trap / future bass)**
1. Piano Roll: notas largas entre **C1 y G1** que sigan la raíz de los acordes.
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
