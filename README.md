# 🚀 ESP32 BOOTLOADER

**[ENGLISH](#-english) | [PORTUGUÊS](#-português) | [ESPAÑOL](#-español)**

---

<details open>
<summary><h2>🇺🇸 ENGLISH</h2></summary>

> ⚠️ **WARNING: This project is in ULTRA SUPER BETA stage.** Use at your own risk, bugs are expected, things will break, and everything can change at any time. You have been warned! 😄

## 1. What is it?

The **ESP32 Bootloader** is a firmware for the **TTGO VGA32 v1.4** (or any ESP32) that turns the ESP32 into an emulator loader (or any other software) via SD card.

The idea is simple: instead of needing a computer to flash a different emulator onto the ESP32 every time, you simply insert an SD card with the desired firmware, power up the device, and it does everything automatically — checks if the firmware is new, flashes it if necessary, and starts the emulator.

The bootloader occupies the `factory` partition of the ESP32. Emulators are flashed to the `ota_0` partition. Next time you power up with the same firmware, it goes directly to the emulator without going through the bootloader. If you swap the card, it loads the new firmware. Pretty much like Multicore does.

## 2. How does it work?

### Boot flow

```
Power on ESP32
    │
    ▼
Display splash screen (ESP32 BOOTLOADER)
    │
    ▼
Check SD card
    ├── Not found → restart in 5s
    └── Found
            │
            ▼
        Read version.txt
            │
            ├── Version matches stored → boot directly to emulator ✅
            └── Different version → flash firmware.bin → boot to emulator ✅
```

### SD card structure

| File | Description |
|---|---|
| `firmware.bin` | The emulator firmware |
| `version.txt` | A simple text with the firmware name/version (e.g.: `ESPectrum_1.4.5`) |

### About FabGL and PS2Controller

The bootloader uses the **FabGL** library to display the splash screen via VGA. FabGL normally initializes the PS2 controller (keyboard) using the **ULP** (Ultra Low Power coprocessor) of the ESP32 — which caused conflicts with emulators that also need the PS2/ULP.

The solution was to locally modify the `ps2controller.cpp` of FabGL so that `begin()` does not initialize the ULP. With this, the keyboard of the emulators works normally after boot (God willing!).

### About firmware.bin

**Do not use the web flasher `.bin` directly!** For **ESPectrum**, use the `.upg` file directly — just rename it to `firmware.bin`. For **MSPX** and **CPC**, you need to extract the app from the merged `.bin` (see section 4.1).

## 3. How to flash the bootloader?

**Option 1 — Web Flasher:** [alternativebits.com/esp32](https://alternativebits.com/esp32)

**Option 2 — Compile yourself:**
```bash
git clone https://github.com/fg1998/esp32-bootloader.git
cd esp32-bootloader
pio run --target upload
```

## 4. Where to find the emulators?

- **ESPectrum:** [zxespectrum.speccy.org/flash](https://zxespectrum.speccy.org/flash/) — get the `.upg` file · [Direct download](https://alternativebits.com/ESP32/ESPectrum_1.4.5.zip)
- **CPC:** [Direct download](https://alternativebits.com/ESP32/CPCESP_0.85.zip)
- **MSPX:** Available only to Eremus sponsors. Link coming soon.

## 4.1. How to extract the correct firmware.bin

### For MSPX and CPC (offset 0x40000)
```python
with open('firmware_merged.bin', 'rb') as f:
    data = f.read()
app = data[0x40000:]
with open('firmware.bin', 'wb') as f:
    f.write(app)
print(f'firmware.bin: {len(app)//1024} KB')
```

### To find the correct offset for any .bin
```python
with open('firmware_merged.bin', 'rb') as f:
    data = f.read()
print(f'Total size: {len(data)} bytes ({len(data)//1024} KB)')
for offset in [0x0000, 0x1000, 0x8000, 0xe000, 0x10000, 0x40000, 0x90000, 0xa0000]:
    if offset < len(data):
        print(f'offset 0x{offset:05X}: 0x{data[offset]:02X}')
```
Look for offsets returning `0xE9`. The **first** is the bootloader (skip). The **second** is the app (use this).

## 5. Which emulators work?

| Emulator | Repository | Status |
|---|---|---|
| **ESPectrum** (ZX Spectrum 48K/128K) | [EremusOne/ESPectrum](https://github.com/EremusOne/ESPectrum) | ✅ Working |
| **CPC** (Amstrad CPC) | EremusOne/CPCEsp | ✅ Working |
| **MSPX** (MSX) | EremusOne/MSPX | ✅ Working |

## 6. Known issues

- ⚠️ **PS2 Keyboard** — inconsistent behavior. If keyboard doesn't respond after boot, power off and on again.
- ⚠️ **PocketTRS** — not compatible yet due to WiFi conflicts and partition differences.
- ⚠️ **First flash after erase** — some emulators save settings in NVS. After erase flash, settings are lost.
- ⚠️ **ESPectrum self-update** — no longer works due to partition changes. Use the bootloader to update instead.

## 7. ULTRA SUPER BETA

**It can crash · The API may change · It has bugs · But it works!** — most of the time.

## 8. Credits

- **[EremusOne](https://github.com/EremusOne)** — for ESPectrum, CPCEsp and MSPX
- **[fdivitto (FabGL)](https://github.com/fdivitto/fabgl)** — for the FabGL library

## 9. If you liked it, you know what to do!

Consider a donation through **[this link](https://github.com/sponsors/fg1998)**. I will spend all donated money on BEER 🍺

*by Fernando Garcia — [fg1998](https://github.com/fg1998)*

</details>

---

<details>
<summary><h2>🇧🇷 PORTUGUÊS</h2></summary>

> ⚠️ **ATENÇÃO: Este projeto está em estágio ULTRA SUPER BETA.** Use por sua conta e risco, bugs são esperados, coisas vão quebrar, e tudo pode mudar a qualquer momento. Você foi avisado! 😄

## 1. O que é?

O **ESP32 Bootloader** é um firmware para o **TTGO VGA32 v1.4** (ou qualquer ESP32) que transforma o ESP32 em um carregador de emuladores (ou qualquer outro software) via cartão SD.

A ideia é simples: em vez de precisar de um computador para gravar um emulador diferente no ESP32 toda vez, você simplesmente coloca um cartão SD com o firmware desejado, liga o aparelho, e ele faz tudo automaticamente — verifica se o firmware é novo, grava se necessário, e inicia o emulador.

O bootloader ocupa a partição `factory` do ESP32. Os emuladores são gravados na partição `ota_0`. Na próxima vez que você ligar com o mesmo firmware, ele vai direto para o emulador sem passar pelo bootloader de forma demorada. Se trocar o cartão, vai carregar um firmware novo. Mais ou menos como o Multicore faz.

## 2. Como funciona?

### Fluxo de inicialização

```
Liga o ESP32
    │
    ▼
Exibe splash screen (ESP32 BOOTLOADER)
    │
    ▼
Verifica cartão SD
    ├── Não encontrado → reinicia em 5s
    └── Encontrado
            │
            ▼
        Lê version.txt
            │
            ├── Versão igual à gravada → boot direto no emulador ✅
            └── Versão diferente → grava firmware.bin → boot no emulador ✅
```

### Estrutura do cartão SD

| Arquivo | Descrição |
|---|---|
| `firmware.bin` | O firmware do emulador |
| `version.txt` | Um texto simples com o nome/versão do firmware (ex: `ESPectrum_1.4.5`) |

### Sobre a FabGL e o PS2Controller

O bootloader usa a biblioteca **FabGL** para exibir a splash screen via VGA. A FabGL normalmente inicializa o controlador PS2 (teclado) usando o **ULP** (Ultra Low Power coprocessor) do ESP32 — o que causava conflito com os emuladores que também precisam do PS2/ULP.

A solução foi modificar o `ps2controller.cpp` da FabGL localmente para que o `begin()` não inicialize o ULP. Com isso, o teclado dos emuladores funciona normalmente após o boot (Se Deus quiser!).

### Sobre o firmware.bin

**Não use o `.bin` do web flasher diretamente!** Para o **ESPectrum**, use o arquivo `.upg` diretamente — só renomeie para `firmware.bin`. Para **MSPX** e **CPC**, é necessário extrair o app do `.bin` merged (veja seção 4.1).

## 3. Como gravar o bootloader?

**Opção 1 — Web Flasher:** [alternativebits.com/esp32](https://alternativebits.com/esp32)

**Opção 2 — Compilando você mesmo:**
```bash
git clone https://github.com/fg1998/esp32-bootloader.git
cd esp32-bootloader
pio run --target upload
```

## 4. Onde encontrar os emuladores?

- **ESPectrum:** [zxespectrum.speccy.org/flash](https://zxespectrum.speccy.org/flash/) — pegue o `.upg` · [Download direto](https://alternativebits.com/ESP32/ESPectrum_1.4.5.zip)
- **CPC:** [Download direto](https://alternativebits.com/ESP32/CPCESP_0.85.zip)
- **MSPX:** Disponível apenas para patrocinadores do Eremus. Link em breve.

## 4.1. Como extrair o firmware.bin correto do arquivo merged

### Para MSPX e CPC (offset 0x40000)
```python
with open('firmware_merged.bin', 'rb') as f:
    data = f.read()
app = data[0x40000:]
with open('firmware.bin', 'wb') as f:
    f.write(app)
print(f'firmware.bin: {len(app)//1024} KB')
```

### Para descobrir o offset correto de qualquer .bin
```python
with open('firmware_merged.bin', 'rb') as f:
    data = f.read()
print(f'Tamanho total: {len(data)} bytes ({len(data)//1024} KB)')
for offset in [0x0000, 0x1000, 0x8000, 0xe000, 0x10000, 0x40000, 0x90000, 0xa0000]:
    if offset < len(data):
        print(f'offset 0x{offset:05X}: 0x{data[offset]:02X}')
```
Procure pelos offsets que retornam `0xE9`. O **primeiro** é o bootloader (não usar). O **segundo** é o app (usar esse).

## 5. Quais emuladores funcionam?

| Emulador | Repositório | Status |
|---|---|---|
| **ESPectrum** (ZX Spectrum 48K/128K) | [EremusOne/ESPectrum](https://github.com/EremusOne/ESPectrum) | ✅ Funcionando |
| **CPC** (Amstrad CPC) | EremusOne/CPCEsp | ✅ Funcionando |
| **MSPX** (MSX) | EremusOne/MSPX | ✅ Funcionando |

## 6. Problemas conhecidos

- ⚠️ **Teclado PS2** — comportamento inconsistente. Se o teclado não responder após o boot, tente desligar e ligar novamente.
- ⚠️ **PocketTRS** — não compatível ainda, devido a conflitos de WiFi e diferenças na estrutura de partições.
- ⚠️ **Primeira gravação após erase flash** — alguns emuladores salvam configurações na NVS. Após erase flash, as configurações são perdidas.
- ⚠️ **Atualização do ESPectrum pelo próprio emulador** — não funciona mais devido à mudança de partições. Use o bootloader para atualizar.

## 7. ULTRA SUPER BETA

**Pode crashar · A API pode mudar · Tem bugs · Mas funciona!** — na maior parte do tempo.

## 8. Agradecimentos

- **[EremusOne](https://github.com/EremusOne)** — pelos emuladores ESPectrum, CPCEsp e MSPX
- **[fdivitto (FabGL)](https://github.com/fdivitto/fabgl)** — pela biblioteca FabGL

## 9. Se você gostou, já sabe!

Considere uma doação através **[desse link](https://github.com/sponsors/fg1998)**. Esse bootloader deu muito mais trabalho que eu imaginava. Eu gastarei toda a grana doada em CERVEJA 🍺

*by Fernando Garcia — [fg1998](https://github.com/fg1998)*

</details>

---

<details>
<summary><h2>🇪🇸 ESPAÑOL</h2></summary>

> ⚠️ **ATENCIÓN: Este proyecto está en etapa ULTRA SUPER BETA.** Úsalo bajo tu propio riesgo, se esperan errores, las cosas se romperán y todo puede cambiar en cualquier momento. ¡Estás avisado! 😄

## 1. ¿Qué es?

El **ESP32 Bootloader** es un firmware para el **TTGO VGA32 v1.4** (o cualquier ESP32) que convierte el ESP32 en un cargador de emuladores (o cualquier otro software) mediante tarjeta SD.

La idea es simple: en lugar de necesitar una computadora para grabar un emulador diferente en el ESP32 cada vez, simplemente insertas una tarjeta SD con el firmware deseado, enciendes el dispositivo, y él hace todo automáticamente — verifica si el firmware es nuevo, lo graba si es necesario, y arranca el emulador.

El bootloader ocupa la partición `factory` del ESP32. Los emuladores se graban en la partición `ota_0`. La próxima vez que enciendas con el mismo firmware, va directamente al emulador. Si cambias la tarjeta, carga el nuevo firmware. Más o menos como hace el Multicore.

## 2. ¿Cómo funciona?

### Flujo de arranque

```
Encender ESP32
    │
    ▼
Mostrar splash screen (ESP32 BOOTLOADER)
    │
    ▼
Verificar tarjeta SD
    ├── No encontrada → reiniciar en 5s
    └── Encontrada
            │
            ▼
        Leer version.txt
            │
            ├── Versión igual a la grabada → arrancar directo al emulador ✅
            └── Versión diferente → grabar firmware.bin → arrancar al emulador ✅
```

### Estructura de la tarjeta SD

| Archivo | Descripción |
|---|---|
| `firmware.bin` | El firmware del emulador |
| `version.txt` | Un texto simple con el nombre/versión del firmware (ej: `ESPectrum_1.4.5`) |

### Sobre FabGL y PS2Controller

El bootloader usa la biblioteca **FabGL** para mostrar la splash screen por VGA. FabGL normalmente inicializa el controlador PS2 (teclado) usando el **ULP** del ESP32 — lo que causaba conflictos con los emuladores. La solución fue modificar localmente el `ps2controller.cpp` de FabGL para que `begin()` no inicialice el ULP (¡si Dios quiere!).

### Sobre firmware.bin

**¡No uses el `.bin` del web flasher directamente!** Para **ESPectrum**, usa el `.upg` directamente — solo renómbralo a `firmware.bin`. Para **MSPX** y **CPC**, extrae el app del `.bin` merged (ver sección 4.1).

## 3. ¿Cómo grabar el bootloader?

**Opción 1 — Web Flasher:** [alternativebits.com/esp32](https://alternativebits.com/esp32)

**Opción 2 — Compilando tú mismo:**
```bash
git clone https://github.com/fg1998/esp32-bootloader.git
cd esp32-bootloader
pio run --target upload
```

## 4. ¿Dónde encontrar los emuladores?

- **ESPectrum:** [zxespectrum.speccy.org/flash](https://zxespectrum.speccy.org/flash/) — obtén el `.upg` · [Descarga directa](https://alternativebits.com/ESP32/ESPectrum_1.4.5.zip)
- **CPC:** [Descarga directa](https://alternativebits.com/ESP32/CPCESP_0.85.zip)
- **MSPX:** Disponible solo para patrocinadores de Eremus. Enlace próximamente.

## 4.1. Cómo extraer el firmware.bin correcto

### Para MSPX y CPC (offset 0x40000)
```python
with open('firmware_merged.bin', 'rb') as f:
    data = f.read()
app = data[0x40000:]
with open('firmware.bin', 'wb') as f:
    f.write(app)
print(f'firmware.bin: {len(app)//1024} KB')
```

### Para descubrir el offset correcto de cualquier .bin
```python
with open('firmware_merged.bin', 'rb') as f:
    data = f.read()
print(f'Tamaño total: {len(data)} bytes ({len(data)//1024} KB)')
for offset in [0x0000, 0x1000, 0x8000, 0xe000, 0x10000, 0x40000, 0x90000, 0xa0000]:
    if offset < len(data):
        print(f'offset 0x{offset:05X}: 0x{data[offset]:02X}')
```
Busca los offsets que devuelven `0xE9`. El **primero** es el bootloader (no usar). El **segundo** es el app (usar este).

## 5. ¿Qué emuladores funcionan?

| Emulador | Repositorio | Estado |
|---|---|---|
| **ESPectrum** (ZX Spectrum 48K/128K) | [EremusOne/ESPectrum](https://github.com/EremusOne/ESPectrum) | ✅ Funcionando |
| **CPC** (Amstrad CPC) | EremusOne/CPCEsp | ✅ Funcionando |
| **MSPX** (MSX) | EremusOne/MSPX | ✅ Funcionando |

## 6. Problemas conocidos

- ⚠️ **Teclado PS2** — comportamiento inconsistente. Si el teclado no responde, apaga y enciende de nuevo.
- ⚠️ **PocketTRS** — aún no compatible debido a conflictos de WiFi y diferencias en particiones.
- ⚠️ **Primera grabación después de erase flash** — algunos emuladores pierden configuraciones guardadas en NVS.
- ⚠️ **Actualización del ESPectrum** — ya no funciona por el cambio de particiones. Usa el bootloader para actualizar.

## 7. ULTRA SUPER BETA

**Puede crashear · La API puede cambiar · Tiene bugs · ¡Pero funciona!** — la mayor parte del tiempo.

## 8. Agradecimientos

- **[EremusOne](https://github.com/EremusOne)** — por los emuladores ESPectrum, CPCEsp y MSPX
- **[fdivitto (FabGL)](https://github.com/fdivitto/fabgl)** — por la biblioteca FabGL

## 9. ¡Si te gustó, ya sabes!

Considera una donación a través de **[este enlace](https://github.com/sponsors/fg1998)**. Gastaré todo el dinero donado en CERVEZA 🍺

*by Fernando Garcia — [fg1998](https://github.com/fg1998)*

</details>
