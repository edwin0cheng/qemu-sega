/*
 * QEMU Sega Genesis Controllers Emulator
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

#include "hw/m68k/genesis.h"

#define DPRINTF(fmt, ...)

OBJECT_DECLARE_SIMPLE_TYPE(GenesisCtrlsState, GENESIS_CTRLS)

#define CONTROLLERS_SIZE 0x30

#define REG_VERSION 0x01
#define REG_DATA1 0x03
#define REG_DATA2 0x05
#define REG_DATA3 0x07
#define REG_CTRL1 0x09
#define REG_CTRL2 0x0B
#define REG_CTRL3 0x0D
#define REG_S_CTRL1 0x13
#define REG_S_CTRL2 0x19
#define REG_S_CTRL3 0x1F

typedef struct
{
    uint16_t buttons;
    uint8_t ctrl;
    uint8_t th_count;
    uint8_t next_read;

    uint8_t s_ctrl;
} GenesisControllerPort;

struct GenesisCtrlsState
{
    SysBusDevice sbd;
    MemoryRegion mr;

    GenesisControllerPort port[2];
    GenesisControllerPort expansion;
} ;

static uint8_t get_port_data(GenesisControllerPort *port)
{
    // TODO: Implement this, for now just return 0
    DPRINTF("WARN: get_port_data not implemented.\n");
    return 0;
}

static void set_port_data(GenesisControllerPort *port, uint64_t value)
{
    // TODO: Implement this, do nothing for now
    DPRINTF("WARN: set_port_data not implemented.\n");
}

static void set_port_ctrl(GenesisControllerPort *port, uint8_t value)
{
    // TODO: Implement this, do nothing for now
    DPRINTF("WARN: set_port_ctrl not implemented.\n");
}

static uint8_t ctrls_read_u8(void *opaque, hwaddr addr)
{
    GenesisCtrlsState *self = ((GenesisCtrlsState *)opaque);

    switch (addr)
    {
    case REG_VERSION - 1:
    case REG_VERSION:
        // Overseas Version, NTSC, No Expansion
        return 0xA0;
    case REG_DATA1 - 1:
    case REG_DATA1:
        return get_port_data(&self->port[0]);
    case REG_DATA2 - 1:
    case REG_DATA2:
        return get_port_data(&self->port[1]);
    case REG_DATA3 - 1:
    case REG_DATA3:
        return get_port_data(&self->expansion);
    case REG_CTRL1 - 1:
    case REG_CTRL1:
        return self->port[0].ctrl;
    case REG_CTRL2 - 1:
    case REG_CTRL2:
        return self->port[1].ctrl;
    case REG_CTRL3 - 1:
    case REG_CTRL3:
        return self->expansion.ctrl;
    case REG_S_CTRL1 - 1:
    case REG_S_CTRL1:
        return self->port[0].s_ctrl | 0x02;
    case REG_S_CTRL2 - 1:
    case REG_S_CTRL2:
        return self->port[1].s_ctrl | 0x02;
    case REG_S_CTRL3 - 1:
    case REG_S_CTRL3:
        return self->expansion.s_ctrl | 0x02;
    default:
        DPRINTF("ctrls_read: !!! unhandled reading from %llx", addr);
        g_assert(!"ctrls_read");
        return 0;
    }
}

static MemTxResult ctrls_read(void *opaque, hwaddr addr, uint64_t *data,
                              unsigned size, MemTxAttrs attrs)
{
    uint8_t r = ctrls_read_u8(opaque, addr);

    if (size == 1)
    {
        *data = r;
    }
    else if (size == 2)
    {
        *data = r;
    }
    else
    {
        DPRINTF("ctrls_read: %08llx\n", addr);
        g_assert(!"not supported");
    }

    DPRINTF("genesis_controller: read from register %llx the value %llx\n", addr, *data);
    return MEMTX_OK;
}

static MemTxResult ctrls_write(void *opaque, hwaddr addr, uint64_t value,
                               unsigned size, MemTxAttrs attrs)
{
    GenesisCtrlsState *self = ((GenesisCtrlsState *)opaque);
    DPRINTF("genesis_controller: write from register %llx the value %llx\n", addr, value);

    switch (addr)
    {
    case REG_DATA1:
        set_port_data(&self->port[0], value);
        break;
    case REG_DATA2:
        set_port_data(&self->port[1], value);
        break;
    case REG_DATA3:
        set_port_data(&self->expansion, value);
        break;
    case REG_CTRL1:
        set_port_ctrl(&self->port[0], value);
        break;
    case REG_CTRL2:
        set_port_ctrl(&self->port[1], value);
        break;
    case REG_CTRL3:
        set_port_ctrl(&self->expansion, value);
        break;
    case REG_S_CTRL1:
        self->port[0].s_ctrl = value & 0xF8;
        break;
    case REG_S_CTRL2:
        self->port[1].s_ctrl = value & 0xF8;
        break;
    case REG_S_CTRL3:
        self->expansion.s_ctrl = value & 0xF8;
        break;
    default:
        DPRINTF("ctrls_write: %08llx\n", addr);
        g_assert(!"not supported");
        break;
    }

    return MEMTX_OK;
}

static const MemoryRegionOps ctrls_ops = {
    .read_with_attrs = ctrls_read,
    .write_with_attrs = ctrls_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 2,
    },
};

static void genesis_ctrls_reset(DeviceState *dev)
{
    GenesisCtrlsState *s = GENESIS_CTRLS(dev);

    memset(&s->port[0], 0, sizeof(s->port[0]));
    memset(&s->port[1], 0, sizeof(s->port[1]));
    memset(&s->expansion, 0, sizeof(s->expansion));
    
    s->port[0].buttons = 0xffff;
    s->port[1].buttons = 0xffff;
    s->expansion.buttons = 0xffff;
}

static void genesis_ctrls_realize(DeviceState *dev, Error **errp)
{
    GenesisCtrlsState *s = GENESIS_CTRLS(dev);

    // memory_region_init_io(mr, NULL, &ctrls_ops, ctrls, "Genesis Controller", CONTROLLERS_SIZE);
    // memory_region_add_subregion(parent_mr, offset, mr);

    memory_region_init_io(&s->mr, OBJECT(dev), &ctrls_ops, s, "genesis.ctrls", CONTROLLERS_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mr);

    // qemu_add_kbd_event_handler(genesis_ctrls_event, s);
}

static const VMStateDescription genesis_ctrls_vmstate = {
    .name = TYPE_GENESIS_CTRLS,
    .unmigratable = 1, /* TODO: Implement this when m68k CPU is migratable */
};

static void genesis_ctrls_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
    dc->vmsd = &genesis_ctrls_vmstate;
    dc->realize = genesis_ctrls_realize;
    dc->reset = genesis_ctrls_reset;
}

static const TypeInfo genesis_ctrls_info = {
    .name = TYPE_GENESIS_CTRLS,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(GenesisCtrlsState),
    .class_init = genesis_ctrls_class_init,
};

static void genesis_ctrls_register_types(void)
{
    type_register_static(&genesis_ctrls_info);
}

type_init(genesis_ctrls_register_types)