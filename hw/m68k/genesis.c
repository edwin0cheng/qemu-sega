/*
 * QEMU Sega Genesis hardware System Emulator
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
#include "qom/object.h"
#include "hw/boards.h"
#include "target/m68k/cpu.h"
#include "sysemu/reset.h"
#include "hw/loader.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/qdev-properties.h"
#include "hw/m68k/genesis.h"

// #define DPRINTF printf
#define DPRINTF(fmt, ...) 

#define TYPE_GENESIS_MACHINE MACHINE_TYPE_NAME("sega-genesis")
OBJECT_DECLARE_SIMPLE_TYPE(GenesisState, GENESIS_MACHINE)

#define ROM_SIZE 0x00400000
#define RAM_SIZE 0x00010000
#define COPROCESSOR_RAM_SIZE 0x00010000
#define COPROCESSOR_BUS_SIZE 0x4000

#define IO_BASE 0x00A00000
#define COPROCESSOR_RAM_BASE IO_BASE
#define CONTROLLERS_BASE 0x00A10000
#define COPROCESSOR_BUS_BASE 0x00A11000
#define YM7101_BASE 0x00C00000
#define RAM_BASE 0x00FF0000

typedef struct
{
    M68kCPU *cpu;
    hwaddr initial_pc;
    hwaddr initial_stack;
} ResetInfo;

typedef struct CoprocessorBus
{
    bool bus_request;
    bool reset;
} Coprocessor;

typedef struct IODevices
{
    Coprocessor coprocessor;
} IODevices;

struct GenesisState
{
    MachineState parent;
    MemoryRegion rom;
    MemoryRegion ram;

    MemoryRegion io_all;

    MemoryRegion coprocessor_ram;
    MemoryRegion ctrls;
    MemoryRegion coprocessor_bus;

    IODevices io_devices;
};

static void main_cpu_reset(void *opaque)
{
    ResetInfo *reset_info = opaque;
    M68kCPU *cpu = reset_info->cpu;
    CPUState *cs = CPU(cpu);

    DPRINTF("main_cpu_reset, %s\n", cs->as->name);

    DPRINTF("before cpu->env.aregs[7]: %08x\n", cpu->env.aregs[7]);
    DPRINTF("before cpu->env.pc: %08x\n", cpu->env.pc);
    cpu_reset(cs);
    cpu->env.aregs[7] = reset_info->initial_stack;
    cpu->env.pc = reset_info->initial_pc;
    cpu->env.sr = 0x2700;

    DPRINTF("cpu->env.aregs[7]: %08x\n", cpu->env.aregs[7]);
    DPRINTF("cpu->env.pc: %08x\n", cpu->env.pc);
}

uint8_t io_memory[RAM_BASE - IO_BASE];

static MemTxResult io_all_read(void *opaque, hwaddr addr, uint64_t *data,
                               unsigned size, MemTxAttrs attrs)

{
    MemTxResult r;
    // uint32_t val;
    hwaddr o_addr = addr + IO_BASE;
    // size_t i = 0;

    DPRINTF("io_all_read: %08llx\n", o_addr);
    g_assert(!"io_all_read");

    // *data = val;
    r = MEMTX_OK;
    return r;
}

static MemTxResult io_all_write(void *opaque, hwaddr addr, uint64_t value,
                                unsigned size, MemTxAttrs attrs)
{
    MemTxResult r;

    DPRINTF("io_all_write: %08llx\n", addr + IO_BASE);
    g_assert(!"io_all_write");

    r = MEMTX_OK;
    return r;
}

static const MemoryRegionOps io_all_ops = {
    .read_with_attrs = io_all_read,
    .write_with_attrs = io_all_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 2,
    },
};

static MemTxResult coprocessor_read(void *opaque, hwaddr addr, uint64_t *data,
                                    unsigned size, MemTxAttrs attrs)
{
    GenesisState *m = (GenesisState *)opaque;
    uint8_t val;
    Coprocessor *self = &m->io_devices.coprocessor;

    DPRINTF("coprocessor_read: %08llx\n", addr);

    switch (addr)
    {
    case 0x100:
        val = 0;
        if (self->bus_request && self->reset)
        {
            val = 0x01;
        }
        *data = ldub_p(&val);
        break;
    default:
        g_assert(!"unhandled coprocessor_read");
        break;
    }

    return MEMTX_OK;
}

static MemTxResult coprocessor_write(void *opaque, hwaddr addr, uint64_t value,
                                     unsigned size, MemTxAttrs attrs)
{
    GenesisState *m = (GenesisState *)opaque;
    Coprocessor *self = &m->io_devices.coprocessor;

    DPRINTF("coprocessor_write: %08llx\n", addr);

    switch (addr)
    {
    case 0x000:
        // ROM vs DRAM mode (not implemented)
        break;
    case 0x100:
        self->bus_request = (value != 0);
        break;
    case 0x200:
        self->reset = (value == 0);
        break;
    default:
        g_assert(!"unhandled coprocessor_write");
        break;
    }

    return MEMTX_OK;
}

static const MemoryRegionOps coprocessor_ops = {
    .read_with_attrs = coprocessor_read,
    .write_with_attrs = coprocessor_write,
    .endianness = DEVICE_BIG_ENDIAN,
};

#define TYPE_GENESIS_PC "genesis-pc"
OBJECT_DECLARE_SIMPLE_TYPE(GenesisPC, GENESIS_PC)

/* Genesis Peripheral Controller */
struct GenesisPC
{
    SysBusDevice parent_obj;

    M68kCPU *cpu;
};

static void genesis_pc_reset(DeviceState *dev)
{
    // nothing to reset
}

static void genesis_pc_realize(DeviceState *dev, Error **errp)
{
    // nothing to realize
}

/*
 * If the m68k CPU implemented its inbound irq lines as GPIO lines
 * rather than via the m68k_set_irq_level() function we would not need
 * this cpu link property and could instead provide outbound IRQ lines
 * that the board could wire up to the CPU.
 */
static Property genesis_pc_properties[] = {
    DEFINE_PROP_LINK("cpu", GenesisPC, cpu, TYPE_M68K_CPU, M68kCPU *),
    DEFINE_PROP_END_OF_LIST(),
};

static void genesis_pc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Sega Genesis Peripheral Controller";
    dc->realize = genesis_pc_realize;
    dc->reset = genesis_pc_reset;
    device_class_set_props(dc, genesis_pc_properties);
}

static const TypeInfo genesis_pc_info = {
    .name = TYPE_GENESIS_PC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(GenesisPC),
    .class_init = genesis_pc_class_init,
};

static void sega_genesis_init(MachineState *machine)
{
    GenesisState *m = GENESIS_MACHINE(machine);
    CPUM68KState *env;
    SysBusDevice *sysbus;
    M68kCPU *cpu;
    ssize_t ret;
    uint8_t *ptr;
    DeviceState *pcdev, *ym7101;
    MemoryRegion *sysmem = get_system_memory();
    ResetInfo *reset_info = g_new0(ResetInfo, 1);

    m->io_devices.coprocessor.bus_request = true;
    m->io_devices.coprocessor.reset = true;

    DPRINTF("sega_genesis_init\n");

    /* Initialize the cpu core */
    cpu = M68K_CPU(cpu_create(machine->cpu_type));
    if (!cpu)
    {
        error_report("Unable to find m68k CPU definition");
        exit(1);
    }
    env = &cpu->env;

    /* Peripheral Controller */
    pcdev = qdev_new(TYPE_GENESIS_PC);
    object_property_set_link(OBJECT(pcdev), "cpu", OBJECT(cpu), &error_abort);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(pcdev), &error_fatal);

    /* Initialize the cpu core */
    qemu_register_reset(main_cpu_reset, reset_info);

    /* ROM */
    memory_region_init_rom(&m->rom, NULL, "sega.rom", ROM_SIZE, &error_fatal);
    memory_region_add_subregion(sysmem, 0, &m->rom);

    /* RAM */
    memory_region_init_ram(&m->ram, NULL, "sega-genesis.ram",
                           RAM_SIZE, &error_fatal);
    memory_region_add_subregion(sysmem, RAM_BASE, &m->ram);

    /* IO */
    memory_region_init_io(&m->io_all, NULL, &io_all_ops, NULL,
                          "Sega IO", RAM_BASE - IO_BASE);
    memory_region_add_subregion(sysmem, IO_BASE,
                                &m->io_all);

    /* Z80 coprocessor ram*/
    memory_region_init_ram(&m->coprocessor_ram, NULL, "coprocessor.ram",
                           COPROCESSOR_RAM_SIZE, &error_fatal);
    memory_region_add_subregion(&m->io_all, COPROCESSOR_RAM_BASE - IO_BASE, &m->coprocessor_ram);

    /* Controllers */
    sysbus_create_simple(TYPE_GENESIS_CTRLS, CONTROLLERS_BASE, NULL);

    /* Z80 coprocessor */
    memory_region_init_io(&m->coprocessor_bus, NULL, &coprocessor_ops, m,
                          "Z80 Coprocessor Bus", COPROCESSOR_BUS_SIZE);
    memory_region_add_subregion(&m->io_all, COPROCESSOR_BUS_BASE - IO_BASE, &m->coprocessor_bus);

    /* VDP */
    // sysbus_create_simple(TYPE_YM7101, YM7101_BASE, NULL);

    ym7101 = qdev_new(TYPE_YM7101);
    object_property_set_link(OBJECT(ym7101), "cpu", OBJECT(cpu), &error_abort);
    sysbus = SYS_BUS_DEVICE(ym7101);
    sysbus_realize_and_unref(sysbus, &error_fatal);
    sysbus_mmio_map(sysbus, 0, YM7101_BASE);

    // ret = load_image_mr("../test-roms/demo.bin", &m->rom);
    ret = load_image_targphys("../test-roms/sonic2r.bin", 0, ROM_SIZE);

    if (ret < 0)
    {
        error_report("Unable to load ROM image");
        exit(1);
    }

    DPRINTF("cpu->env: %p\n", env);

    // /* Initialize CPU registers.  */
    ptr = rom_ptr(0, ret);
    assert(ptr != NULL);

    reset_info->cpu = cpu;
    reset_info->initial_pc = ldl_p(ptr + 4);
    reset_info->initial_stack = ldl_p(ptr);

    DPRINTF("load_image_targphys %ld\n", ret);
}

static void sega_genesis_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Sega Genesis";
    mc->init = sega_genesis_init;
    mc->default_cpu_type = M68K_CPU_TYPE_NAME("m68000");
    mc->max_cpus = 1;
}

static const TypeInfo sega_genesis_typeinfo = {
    .name = TYPE_GENESIS_MACHINE,
    .parent = TYPE_MACHINE,
    .class_init = sega_genesis_class_init,
    .instance_size = sizeof(GenesisState),
};

static void sega_genesis_register_type(void)
{
    type_register_static(&sega_genesis_typeinfo);
    type_register_static(&genesis_pc_info);
}

type_init(sega_genesis_register_type)