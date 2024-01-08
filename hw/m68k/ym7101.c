/*
 * QEMU YM7101 Emulator
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/sysbus.h"
#include "ui/console.h"
#include "migration/vmstate.h"
#include "qom/object.h"
#include "target/m68k/cpu.h"
#include "hw/core/cpu.h"
#include "hw/qdev-properties.h"

#include "hw/m68k/genesis.h"

#define PAL_MODE 0x0001
#define DMA_BUSY 0x0002
#define IN_HBLANK 0x0004
#define IN_VBLANK 0x0008
#define ODD_FRAME 0x0010
#define SPRITE_COLLISIO 0x0020
#define SPRITE_OVERFLOW 0x0040
#define V_INTERRUPT 0x0080
#define FIFO_FULL 0x0100
#define FIFO_EMPTY 0x0200

#define REG_MODE_SET_1 0x00
#define REG_MODE_SET_2 0x01
#define REG_SCROLL_A_ADDR 0x02
#define REG_WINDOW_ADDR 0x03
#define REG_SCROLL_B_ADDR 0x04
#define REG_SPRITES_ADDR 0x05
// Register 0x06 Unused
#define REG_BACKGROUND 0x07
// Register 0x08 Unused
// Register 0x09 Unused
#define REG_H_INTERRUPT 0x0A
#define REG_MODE_SET_3 0x0B
#define REG_MODE_SET_4 0x0C
#define REG_HSCROLL_ADDR 0x0D
// Register 0x0E Unused
#define REG_AUTO_INCREMENT 0x0F
#define REG_SCROLL_SIZE 0x10
#define REG_WINDOW_H_POS 0x11
#define REG_WINDOW_V_POS 0x12
#define REG_DMA_COUNTER_LOW 0x13
#define REG_DMA_COUNTER_HIGH 0x14
#define REG_DMA_ADDR_LOW 0x15
#define REG_DMA_ADDR_MID 0x16
#define REG_DMA_ADDR_HIGH 0x17

#define MEMORY_VRAM 0x01
#define MEMORY_CRAM 0x02
#define MEMORY_VSRAM 0x03

#define DMA_TYPE_NONE 0x00
#define DMA_TYPE_MEMORY 0X01
#define DMA_TYPE_FILL 0X02
#define DMA_TYPE_COPY 0X03

OBJECT_DECLARE_SIMPLE_TYPE(Ym7101State, YM7101)

#define YM7101_SIZE 0x20

#define DPRINTF printf
// #define DPRINTF(fmt, ...)

#define PRINT_PC(s) DPRINTF("PC: %08x\n", s->cpu->env.pc)
#define PAUSE()                \
    do                         \
    {                          \
        printf("PAUSE\n");     \
        do                     \
        {                      \
            int c = getchar(); \
            if (c == 'q')      \
                exit(0);       \
            if (c >= 0)        \
                break;         \
        } while (1);           \
    } while (0)

#undef PAUSE
#define PAUSE()

typedef struct
{
    uint8_t vram[0x10000];
    uint8_t cram[128];
    uint8_t vsram[80];

    uint8_t transfer_type;
    uint8_t transfer_bits;
    uint32_t transfer_count;
    uint32_t transfer_remain;
    uint32_t transfer_src_addr;
    uint32_t transfer_dest_addr;
    uint32_t transfer_auto_inc;
    uint16_t transfer_fill_word;
    uint8_t transfer_run;
    uint8_t transfer_target;
    bool transfer_dma_busy;

    uint16_t ctrl_port_buffer;
    bool ctrl_port_set;
} Memory;

typedef struct
{
    uint16_t status;
    Memory memory;

    uint8_t mode_1;
    uint8_t mode_2;
    uint8_t mode_3;
    uint8_t mode_4;

    uint8_t h_int_lines;
    size_t screen_size[2];
    size_t scroll_size[2];
    size_t window_pos[2][2];
    uint8_t window_values[2];
    uint8_t background;
    size_t scroll_a_addr;
    size_t scroll_b_addr;
    size_t window_addr;
    size_t sprites_addr;
    size_t hscroll_addr;

    // sprites: Vec<Sprite>,
    // sprites_by_line: Vec<Vec<usize>>,

    // last_clock: ClockTime,
    // p_clock: u32,
    // h_clock: u32,
    // v_clock: u32,
    uint8_t h_scanlines;

    int32_t current_x;
    int32_t current_y;
} State;

struct Ym7101State
{
    SysBusDevice sbd;
    MemoryRegion mr;
    State state;
    M68kCPU *cpu;
};

static void set_dma_mode(Memory *self, uint8_t mode)
{
    DPRINTF("set_dma_mode\n");
    switch (mode)
    {
    case DMA_TYPE_NONE:
        self->transfer_dma_busy = false;
        self->transfer_run = DMA_TYPE_NONE;
        break;
    default:
        self->transfer_dma_busy = true;
        self->transfer_run = mode;
        break;
    }
}

static uint8_t *get_transfer_target(Memory *self)
{
    switch (self->transfer_target)
    {
    case MEMORY_VRAM:
        return self->vram;
    case MEMORY_CRAM:
        return self->cram;
    case MEMORY_VSRAM:
        return self->vsram;
    default:
        g_assert(!"unhandled transfer target");
        return NULL;
    }
}

static void setup_transfer(Memory *self, uint16_t first, uint16_t second)
{
    self->ctrl_port_buffer = false;

    self->transfer_type = (uint8_t)((((first & 0xC000) >> 14) | ((second & 0x00F0) >> 2)));
    self->transfer_dest_addr = (first & 0x3FFF);
    self->transfer_dest_addr |= ((((uint32_t)second) & 0x0003) << 14);
    switch (self->transfer_type & 0x0E)
    {
    case 0:
        self->transfer_target = MEMORY_VRAM;
        break;
    case 4:
        self->transfer_target = MEMORY_VSRAM;
        break;
    default:
        self->transfer_target = MEMORY_CRAM;
        break;
    }
    DPRINTF("ym7101: transfer requested of type %02x (%02x) to address %04x\n", self->transfer_type, self->transfer_target, self->transfer_dest_addr);

    if (self->transfer_type & 0x20)
    {
        if (self->transfer_type & 0x10)
        {
            set_dma_mode(self, DMA_TYPE_COPY);
        }
        else if (!(self->transfer_bits & 0x80))
        {
            set_dma_mode(self, DMA_TYPE_MEMORY);
        }
    }
}

static void update_screen_size(Ym7101State *self)
{
    // TODO: Implement this
}

static void update_window_position(Ym7101State *self)
{
    // TODO: Implement this
}

static void read_data_port(Ym7101State *self, hwaddr addr, uint64_t *data)
{
    // TODO: Implement this
}

static const char *get_target_name(uint8_t target)
{
    switch (target)
    {
    case MEMORY_VRAM:
        return "vram";
    case MEMORY_CRAM:
        return "cram";
    case MEMORY_VSRAM:
        return "vsram";
    default:
        return "???";
    }

    return "???";
}

static void dump_state(Ym7101State *self)
{
    /*
    Status: Running
    PC: 0x00001506
    SR: 0x2700
    D0: 0x00000000        A0: 0x0001e7db
    D1: 0x00000001        A1: 0xffffaa00
    D2: 0x56000000        A2: 0xfffff700
    D3: 0x00000000        A3: 0x00001504
    D4: 0x47111111        A4: 0x00c00000
    D5: 0x00008540        A5: 0x00000305
    D6: 0x0000000c        A6: 0x00c00004
    D7: 0x00000004       USP: 0x00000000
                        SSP: 0x00fffdc0
                        */

    printf("Status: Running\n");
    printf("PC: 0x%08x\n", self->cpu->env.pc);
    // printf("SR: 0x%04x\n", self->cpu->env.sr);
    printf("D0: 0x%08x        A0: 0x%08x\n", self->cpu->env.dregs[0], self->cpu->env.aregs[0]);
    printf("D1: 0x%08x        A1: 0x%08x\n", self->cpu->env.dregs[1], self->cpu->env.aregs[1]);
    printf("D2: 0x%08x        A2: 0x%08x\n", self->cpu->env.dregs[2], self->cpu->env.aregs[2]);
    printf("D3: 0x%08x        A3: 0x%08x\n", self->cpu->env.dregs[3], self->cpu->env.aregs[3]);
    printf("D4: 0x%08x        A4: 0x%08x\n", self->cpu->env.dregs[4], self->cpu->env.aregs[4]);
    printf("D5: 0x%08x        A5: 0x%08x\n", self->cpu->env.dregs[5], self->cpu->env.aregs[5]);
    printf("D6: 0x%08x        A6: 0x%08x\n", self->cpu->env.dregs[6], self->cpu->env.aregs[6]);
    printf("D7: 0x%08x       USP: 0x%08x\n", self->cpu->env.dregs[7], self->cpu->env.sp[0]);
    printf("                     SSP: 0x%08x\n", self->cpu->env.aregs[7]);

    printf("\n");
    printf("Mode1: 0x%02x\n", self->state.mode_1);
    printf("Mode2: 0x%02x\n", self->state.mode_2);
    printf("Mode3: 0x%02x\n", self->state.mode_3);
    printf("Mode4: 0x%02x\n", self->state.mode_4);
    printf("\n");

    printf("Scroll A : 0x%04zx\n", self->state.scroll_a_addr);
    printf("Window   : 0x%04zx\n", self->state.window_addr);
    printf("Scroll B : 0x%04zx\n", self->state.scroll_b_addr);
    printf("HScroll  : 0x%04zx\n", self->state.hscroll_addr);
    printf("Sprites  : 0x%04zx\n", self->state.sprites_addr);
    printf("\n");

    printf("DMA type  : %d\n", self->state.memory.transfer_type);
    printf("DMA Source: 0x%04x\n", self->state.memory.transfer_src_addr);
    printf("DMA Dest  : 0x%04x\n", self->state.memory.transfer_dest_addr);
    printf("DMA Count : 0x%04x\n", self->state.memory.transfer_count);
    printf("Auto-Inc  : 0x%04x\n", self->state.memory.transfer_auto_inc);
}

static void write_data_port(Memory *self, uint32_t value, size_t size)
{
    size_t addr;
    uint8_t *target;
    size_t i;

    if ((self->transfer_type & 0x30) == 0x20)
    {
        g_assert(size <= 4);
        self->ctrl_port_set = false;
        self->transfer_fill_word = (size == 2) ? value : (value & 0xff);

        set_dma_mode(self, DMA_TYPE_FILL);
    }
    else
    {
        DPRINTF("ym7101: data port write %zu bytes to %s:%04x with %08x\n",
                size, get_target_name(self->transfer_target), self->transfer_dest_addr, value);

        addr = self->transfer_dest_addr;
        target = get_transfer_target(self);
        for (i = 0; i < size; i++)
        {
            target[(addr + i) % sizeof(target)] = extract32(value, (size - i - 1) * 8, 8);
        }
        self->transfer_dest_addr += self->transfer_auto_inc;
    }
}

static void write_control_port(Memory *self, uint32_t value, size_t size)
{
    switch (size)
    {
    case 2:
        if (self->ctrl_port_set)
        {
            setup_transfer(self, self->ctrl_port_buffer, value);
        }
        else
        {
            self->ctrl_port_set = true;
            self->ctrl_port_buffer = value;
        }
        // Return to avoid the rest of the function
        return;
    case 4:
        if (!self->ctrl_port_set)
        {
            setup_transfer(self, value >> 16, value & 0xffff);
            return;
        }
        break;
    }

    g_printerr("ym7101: error when writing to control port with %zu bytes of %08x\n", size, value);

    g_assert(!"unhandled write_control_port");
}

static size_t decode_scroll_size(uint8_t size)
{
    switch (size)
    {
    case 0: // 0b00
        return 32;
    case 1: // 0b01
        return 64;
    case 3: // 0b11
        return 128;
    default:
        DPRINTF("invalid scroll size option %d", size);
        assert(!"invalid scroll size option");
        break;
    }

    return 0;
}

static void set_register(Ym7101State *self, uint16_t value)
{
    size_t h, v;
    uint32_t mask = 0;
    size_t reg = ((value & 0x1F00) >> 8);
    uint8_t data = (value & 0x00FF);

    // debug!("{}: register {:x} set to {:x}", DEV_NAME, reg, data);
    DPRINTF("ym7101: register %04zx set to %02x\n", reg, data);

    switch (reg)
    {
    case REG_MODE_SET_1:
        self->state.mode_1 = data;
        break;
    case REG_MODE_SET_2:
        self->state.mode_2 = data;
        update_screen_size(self);
        break;
    case REG_SCROLL_A_ADDR:
        self->state.scroll_a_addr = ((size_t)data) << 10;
        break;
    case REG_WINDOW_ADDR:
        self->state.window_addr = ((size_t)data) << 10;
        break;
    case REG_SCROLL_B_ADDR:
        self->state.scroll_b_addr = ((size_t)data) << 13;
        break;
    case REG_SPRITES_ADDR:
        self->state.sprites_addr = ((size_t)data) << 9;
        break;
    case REG_BACKGROUND:
        self->state.background = data;
        break;
    case REG_H_INTERRUPT:
        self->state.h_int_lines = data;
        break;
    case REG_MODE_SET_3:
        self->state.mode_3 = data;
        break;
    case REG_MODE_SET_4:
        self->state.mode_4 = data;
        update_screen_size(self);
        break;
    case REG_HSCROLL_ADDR:
        self->state.hscroll_addr = ((size_t)data) << 10;
        break;
    case REG_AUTO_INCREMENT:
        self->state.memory.transfer_auto_inc = (uint32_t)data;
        break;
    case REG_SCROLL_SIZE:
        h = decode_scroll_size(data & 0x03);
        v = decode_scroll_size((data >> 4) & 0x03);
        self->state.scroll_size[0] = h;
        self->state.scroll_size[1] = v;
        break;
    case REG_WINDOW_H_POS:
        self->state.window_values[0] = data;
        update_window_position(self);
        break;
    case REG_WINDOW_V_POS:
        self->state.window_values[1] = data;
        update_window_position(self);
        break;
    case REG_DMA_COUNTER_LOW:
        self->state.memory.transfer_count = (self->state.memory.transfer_count & 0xFF00) | ((uint32_t)data);
        self->state.memory.transfer_remain = self->state.memory.transfer_count;
        break;
    case REG_DMA_COUNTER_HIGH:
        self->state.memory.transfer_count = (self->state.memory.transfer_count & 0x00FF) | (((uint32_t)data) << 8);
        self->state.memory.transfer_remain = self->state.memory.transfer_count;
        break;
    case REG_DMA_ADDR_LOW:
        self->state.memory.transfer_src_addr = (self->state.memory.transfer_src_addr & 0xFFFE00) | (((uint32_t)data) << 1);
        break;
    case REG_DMA_ADDR_MID:
        self->state.memory.transfer_src_addr = (self->state.memory.transfer_src_addr & 0xFE01FF) | (((uint32_t)data) << 9);
        break;
    case REG_DMA_ADDR_HIGH:
        if (data & 0x80)
        {
            mask = 0x7F;
        }
        else
        {
            mask = 0x3F;
        }
        self->state.memory.transfer_bits = data & 0xC0;
        self->state.memory.transfer_src_addr = (self->state.memory.transfer_src_addr & 0x01FFFF) | (((uint32_t)(data & mask)) << 17);
        break;
    case 0x6:
    case 0x8:
    case 0x9:
    case 0xE:
        /* Reserved */
        break;
    default:
        DPRINTF("ym7101: unknown register: %04zx\n", reg);
        g_assert(!"unhandled register");
        break;
    }
}

static MemTxResult ym7101_read(void *opaque, hwaddr addr, uint64_t *data,
                               unsigned size, MemTxAttrs attrs)
{
    uint8_t val1, val2;
    uint64_t mask = 0xFFFFFFFF;
    const char *port = "???";
    Ym7101State *self = YM7101(opaque);

    printf("****** PC = 0x%04x READ addr 0x%04llx size %d\n", self->cpu->env.pc, addr, size);

    switch (addr)
    {
    case 0x00:
    case 0x02:
        port = "data port";
        // Read from Data Port
        read_data_port(self, addr, data);
        break;
    case 0x04:
    case 0x05:
    case 0x06:
    case 0x07:
        g_assert(size <= 4 && ((addr - 0x04) + size <= 4));
        DPRINTF("status = %04x\n", self->state.status);

        *data = self->state.status;
        *data <<= 16;
        *data |= self->state.status;
        *data &= mask >> ((addr - 0x04) * 8);
        *data >>= ((0x08 - addr) - size) * 8;

        port = "control port";
        // Read from Control Port
        break;
    case 0x08:
    case 0x0A:
        port = "h/v counter";
        // Read from H/V Counter
        *data = self->state.current_y & 0xff;
        if (size > 1)
        {
            *data = (*data << 8) | ((self->state.current_y >> 1) & 0xff);
        }
        break;
    default:
        DPRINTF("ym7101_read: %08llx\n", addr);
        g_assert(!"unhandled ym7101_read");
        break;
    }

    val1 = (*data >> 8) & 0xff;
    val2 = *data & 0xff;

    PRINT_PC(self);
    DPRINTF("ym7101: %s read %d bytes from %llx with [%d, %d] %llx\n",
            port, size, addr, (int)val1, (int)val2, *data);

    PAUSE();

    dump_state(self);

    return MEMTX_OK;
}

static MemTxResult ym7101_write(void *opaque, hwaddr addr, uint64_t value,
                                unsigned size, MemTxAttrs attrs)
{
    uint8_t val1, val2;
    const char *port = "???";
    Ym7101State *self = YM7101(opaque);
    bool is_reg = false;
    uint16_t flags;

    printf("****** PC = 0x%04x WRITE addr 0x%04llx size %d\n", self->cpu->env.pc, addr, size);

    if(self->cpu->env.pc == 0x000268) {
        printf("****** PC = 0x%04x WRITE addr 0x%04llx size %d\n", self->cpu->env.pc, addr, size);
    }

    switch (addr)
    {
    case 0x00:
    case 0x02:
        port = "data port";
        // Write from Data Port
        write_data_port(&self->state.memory, value, size);
        break;
    case 0x04:
    case 0x06:
        // Write from Control Port
        port = "control port";
        is_reg = ((size == 2 ? value : (value >> 16)) & 0xC000) == 0x8000;

        if (is_reg)
        {
            if (size == 2)
            {
                set_register(self, value);
            }
            else
            {
                set_register(self, value >> 16);
                value &= 0xffff;
                g_assert(((value & 0xC000) == 0x8000) && "unexpected second byte ");
                set_register(self, value);
            }
        }
        else
        {
            write_control_port(&self->state.memory, value, size);
            flags = self->state.memory.transfer_dma_busy ? DMA_BUSY : 0;
            self->state.status = (self->state.status & ~DMA_BUSY) | flags;
        }
        break;
    case 0x11:
    case 0x12:
    case 0x13:
    case 0x14:
    case 0x15:
    case 0x16:
        port = "sound port";
        break;
    default:
        DPRINTF("ym7101_write: %08llx\n", addr);
        g_assert(!"unhandled ym7101_write");
        break;
    }

    val1 = (value >> 8) & 0xff;
    val2 = value & 0xff;

    PRINT_PC(self);
    DPRINTF("ym7101: %s write %d bytes to %llx with [%d, %d] %llx\n",
            port, size, addr, (int)val1, (int)val2, value);

    PAUSE();

    dump_state(self);

    return MEMTX_OK;
}

static const MemoryRegionOps ym7101_ops = {
    .read_with_attrs = ym7101_read,
    .write_with_attrs = ym7101_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 2,
    },
};

static void ym7101_reset(DeviceState *dev)
{
    Ym7101State *s = YM7101(dev);

    memset(&s->state, 0, sizeof(s->state));
    s->state.status = 0x3400 | FIFO_EMPTY;
}

static void ym7101_realize(DeviceState *dev, Error **errp)
{
    Ym7101State *s = YM7101(dev);

    memory_region_init_io(&s->mr, OBJECT(dev), &ym7101_ops, s, "ym7101", YM7101_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mr);
}

static const VMStateDescription ym7101_vmstate = {
    .name = TYPE_YM7101,
    .unmigratable = 1, /* TODO: Implement this when m68k CPU is migratable */
};

static Property ym7101_properties[] = {
    DEFINE_PROP_LINK("cpu", Ym7101State, cpu, TYPE_M68K_CPU, M68kCPU *),
    DEFINE_PROP_END_OF_LIST(),
};

static void ym7101_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    // set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
    dc->vmsd = &ym7101_vmstate;
    dc->realize = ym7101_realize;
    dc->reset = ym7101_reset;

    device_class_set_props(dc, ym7101_properties);
}

static const TypeInfo ym7101_info = {
    .name = TYPE_YM7101,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Ym7101State),
    .class_init = ym7101_class_init,
};

static void ym7101_register_types(void)
{
    type_register_static(&ym7101_info);
}

type_init(ym7101_register_types)