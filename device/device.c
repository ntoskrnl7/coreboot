/*
 * This file is part of the LinuxBIOS project.
 *
 *  (c) 1999--2000 Martin Mares <mj@suse.cz>
 *  (c) 2003 Eric Biederman <ebiederm@xmission.com>
 *  (c) 2003 Linux Networx
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by 
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

/* lots of mods by ron minnich (rminnich@lanl.gov), with 
 * the final architecture guidance from Tom Merritt (tjm@codegen.com)
 * In particular, we changed from the one-pass original version to 
 * Tom's recommended multiple-pass version. I wasn't sure about doing 
 * it with multiple passes, until I actually started doing it and saw
 * the wisdom of Tom's recommendations ...
 *
 * Lots of cleanups by Eric Biederman to handle bridges, and to
 * handle resource allocation for non-pci devices.
 */

#include <console/console.h>
//#include <bitops.h>
#include <arch/io.h>
#include <device/device.h>
#include <device/pci.h>
#include <device/pci_ids.h>
#include <stdlib.h>
#include <string.h>
#include <lib.h>
//#include <smp/spinlock.h>

/** Linked list of ALL devices */
struct device *all_devices = &dev_root;
/** Pointer to the last device -- computed at run time -- no more config tool magic */
struct device **last_dev_p;

/** The upper limit of MEM resource of the devices.
 * Reserve 20M for the system */
#define DEVICE_MEM_HIGH 0xFEBFFFFFUL
/** The lower limit of IO resource of the devices.
 * Reserve 4k for ISA/Legacy devices */
#define DEVICE_IO_START 0x1000

/**
 * @brief Initialization tasks for the device tree code. 
 * 
 * At present, merely sets up last_dev_p, which used to be done by Fucking Magic (FM) in config tool
 * @return none
 *
 */

void
dev_init(void)
{
	struct device *dev;

	for(dev = all_devices; dev; dev = dev->next) {
		last_dev_p = &dev->next;
	}
}

/**
 * @brief Allocate a new device structure.
 * 
 * Allocte a new device structure and attached it to the device tree as a
 * child of the parent bus.
 *
 * @param parent parent bus the newly created device attached to.
 * @param path path to the device to be created.
 *
 * @return pointer to the newly created device structure.
 *
 * @see device_path
 */
//static spinlock_t dev_lock = SPIN_LOCK_UNLOCKED;
struct device * alloc_dev(struct bus *parent, struct device_path *path)
{
	struct device * dev, *child;
	int link;
	static char thissucks[64];
	static int fuck = 0;

//	spin_lock(&dev_lock);	

	/* Find the last child of our parent */
	for(child = parent->children; child && child->sibling; ) {
		child = child->sibling;
	}

	dev = malloc(sizeof(*dev));
	if (dev == 0) {
		die("DEV: out of memory.\n");
	}
	memset(dev, 0, sizeof(*dev));
	memcpy(&dev->path, path, sizeof(*path));

	/* Initialize the back pointers in the link fields */
	for(link = 0; link < MAX_LINKS; link++) {
		dev->link[link].dev  = dev;
		dev->link[link].link = link;
	}

	/* By default devices are enabled */
	dev->enabled = 1;

	/* Add the new device to the list of children of the bus. */
	dev->bus = parent;
	if (child) {
		child->sibling = dev;
	} else {
		parent->children = dev;
	}

	/* Append a new device to the global device list.
	 * The list is used to find devices once everything is set up.
	 */
	*last_dev_p = dev;
	last_dev_p = &dev->next;

	/* give the device a name */
	dev -> dtsname = malloc(32);
        if (dev->dtsname == 0) {
                die("DEV: out of memory.\n");
        }
	sprintf(thissucks, "dynamic %s", dev_path(dev));
	printk(BIOS_INFO, "thissucks is %s dev is %p dev->dtsname is %p\n", thissucks, dev, dev->dtsname);
	sprintf(dev->dtsname, "dynamic %s", dev_path(dev));
	printk(BIOS_INFO, "after sprintf dtsname is %s\n", dev->dtsname);
	memcpy(dev->dtsname, thissucks, strlen(thissucks));
	printk(BIOS_INFO, "after strcpy dtsname is %s\n", dev->dtsname);
	/* FUCK. sprintf doesn't work. */
	dev->dtsname[0] = '0' + fuck++;
	dev->dtsname[1] = 0;

//	spin_unlock(&dev_lock);
	return dev;
}

/**
 * @brief round a number up to an alignment. 
 * @param val the starting value
 * @param roundup Alignment as a power of two
 * @returns rounded up number
 */
static resource_t round(resource_t val, unsigned long pow)
{
	resource_t mask;
	mask = (1ULL << pow) - 1ULL;
	val += mask;
	val &= ~mask;
	return val;
}

/** Read the resources on all devices of a given bus.
 * @param bus bus to read the resources on.
 */
static void read_resources(struct bus *bus)
{
	struct device *curdev;

	printk(BIOS_SPEW, "%s: %s(%s) read_resources bus %d link: %d\n", __func__, bus->dev->dtsname,
		dev_path(bus->dev), bus->secondary, bus->link);

	/* Walk through all of the devices and find which resources they need. */
	for(curdev = bus->children; curdev; curdev = curdev->sibling) {
		unsigned links;
		int i;
		printk(BIOS_SPEW, "%s: %s(%s) have_resources %d enabled %d\n", __func__, bus->dev->dtsname,
			dev_path(bus->dev), curdev->have_resources, curdev->enabled);
		if (curdev->have_resources) {
			continue;
		}
		if (!curdev->enabled) {
			continue;
		}
		if (!curdev->ops || !curdev->ops->phase4_read_resources) {
			printk(BIOS_ERR, "%s: %s(%s) missing phase4_read_resources\n", __func__, 
				curdev->dtsname, dev_path(curdev));
			continue;
		}
		curdev->ops->phase4_read_resources(curdev);
		curdev->have_resources = 1;
		/* Read in subtractive resources behind the current device */
		links = 0;
		for(i = 0; i < curdev->resources; i++) {
			struct resource *resource;
			unsigned link;
			resource = &curdev->resource[i];
			if (!(resource->flags & IORESOURCE_SUBTRACTIVE)) 
				continue;
			link = IOINDEX_SUBTRACTIVE_LINK(resource->index);
			if (link > MAX_LINKS) {
				printk(BIOS_ERR, "%s subtractive index on link: %d\n",
					dev_path(curdev), link);
				continue;
			}
			if (!(links & (1 << link))) {
				links |= (1 << link);
				read_resources(&curdev->link[link]);
			}
		}
	}
	printk(BIOS_SPEW, "%s: %s(%s) read_resources bus %d link: %d done\n", __func__, bus->dev->dtsname,
		dev_path(bus->dev), bus->secondary, bus->link);
}

struct pick_largest_state {
	struct resource *last;
	struct device   *result_dev;
	struct resource *result;
	int seen_last;
};

static void pick_largest_resource(void *gp,
	struct device *dev, struct resource *resource)
{
	struct pick_largest_state *state = gp;
	struct resource *last;
	last = state->last;
	/* Be certain to pick the successor to last */
	if (resource == last) {
		state->seen_last = 1;
		return;
	}
	if (resource->flags & IORESOURCE_FIXED ) return; //skip it 
	if (last && (
		    (last->align < resource->align) ||
		    ((last->align == resource->align) &&
			    (last->size < resource->size)) ||
		    ((last->align == resource->align) &&
			    (last->size == resource->size) &&
			    (!state->seen_last)))) {
		return;
	}
	if (!state->result || 
		(state->result->align < resource->align) ||
		((state->result->align == resource->align) &&
			(state->result->size < resource->size)))
	{
		state->result_dev = dev;
		state->result = resource;
	}    
}

static struct device *largest_resource(struct bus *bus, struct resource **result_res,
	unsigned long type_mask, unsigned long type)
{
	struct pick_largest_state state;

	state.last = *result_res;
	state.result_dev = 0;
	state.result = 0;
	state.seen_last = 0;

	search_bus_resources(bus, type_mask, type, pick_largest_resource, &state);

	*result_res = state.result;
	return state.result_dev;
}

/* Compute allocate resources is the guts of the resource allocator.
 * 
 * The problem.
 *  - Allocate resources locations for every device.
 *  - Don't overlap, and follow the rules of bridges.
 *  - Don't overlap with resources in fixed locations.
 *  - Be efficient so we don't have ugly strategies.
 *
 * The strategy.
 * - Devices that have fixed addresses are the minority so don't
 *   worry about them too much.  Instead only use part of the address
 *   space for devices with programmable addresses.  This easily handles
 *   everything except bridges.
 *
 * - PCI devices are required to have thier sizes and their alignments
 *   equal.  In this case an optimal solution to the packing problem
 *   exists.  Allocate all devices from highest alignment to least
 *   alignment or vice versa.  Use this.
 *
 * - So we can handle more than PCI run two allocation passes on
 *   bridges.  The first to see how large the resources are behind
 *   the bridge, and what their alignment requirements are.  The
 *   second to assign a safe address to the devices behind the
 *   bridge.  This allows me to treat a bridge as just a device with 
 *   a couple of resources, and not need to special case it in the
 *   allocator.  Also this allows handling of other types of bridges.
 *
 */

void compute_allocate_resource(
	struct bus *bus,
	struct resource *bridge,
	unsigned long type_mask,
	unsigned long type)
{
	struct device *dev;
	struct resource *resource;
	resource_t base;
	unsigned long align, min_align;
	min_align = 0;
	base = bridge->base;

	printk(BIOS_SPEW, "%s compute_allocate_%s: base: %08Lx size: %08Lx align: %d gran: %d\n", 
		dev_path(bus->dev),
		(bridge->flags & IORESOURCE_IO)? "io":
		(bridge->flags & IORESOURCE_PREFETCH)? "prefmem" : "mem",
		base, bridge->size, bridge->align, bridge->gran);

	/* We want different minimum alignments for different kinds of
	 * resources.  These minimums are not device type specific
	 * but resource type specific.
	 */
	if (bridge->flags & IORESOURCE_IO) {
		min_align = log2(DEVICE_IO_ALIGN);
	}
	if (bridge->flags & IORESOURCE_MEM) {
		min_align = log2(DEVICE_MEM_ALIGN);
	}

	/* Make certain I have read in all of the resources */
	read_resources(bus);

	/* Remember I haven't found anything yet. */
	resource = 0;

	/* Walk through all the devices on the current bus and 
	 * compute the addresses.
	 */
	while((dev = largest_resource(bus, &resource, type_mask, type))) {
		resource_t size;
		/* Do NOT I repeat do not ignore resources which have zero size.
		 * If they need to be ignored dev->read_resources should not even
		 * return them.   Some resources must be set even when they have
		 * no size.  PCI bridge resources are a good example of this.
		 */
		/* Propogate the resource alignment to the bridge register  */
		if (resource->align > bridge->align) {
			bridge->align = resource->align;
		}

		/* Make certain we are dealing with a good minimum size */
		size = resource->size;
		align = resource->align;
		if (align < min_align) {
			align = min_align;
		}

		if (resource->flags & IORESOURCE_FIXED) {
			continue;
		}

		/* Propogate the resource limit to the bridge register */
		if (bridge->limit > resource->limit) {
			bridge->limit = resource->limit;
		}
		/* Artificially deny limits between DEVICE_MEM_HIGH and 0xffffffff */
		if ((bridge->limit > DEVICE_MEM_HIGH) && (bridge->limit <= 0xffffffff)) {
			bridge->limit = DEVICE_MEM_HIGH;
		}
		if (resource->flags & IORESOURCE_IO) {
			/* Don't allow potential aliases over the
			 * legacy pci expansion card addresses.
			 * The legacy pci decodes only 10 bits,
			 * uses 100h - 3ffh. Therefor, only 0 - ff
			 * can be used out of each 400h block of io
			 * space.
			 */
			if ((base & 0x300) != 0) {
				base = (base & ~0x3ff) + 0x400;
			}
			/* Don't allow allocations in the VGA IO range.
			 * PCI has special cases for that.
			 */
			else if ((base >= 0x3b0) && (base <= 0x3df)) {
				base = 0x3e0;
			}
		}
		if (((round(base, align) + size) -1) <= resource->limit) {
			/* base must be aligned to size */
			base = round(base, align);
			resource->base = base;
			resource->flags |= IORESOURCE_ASSIGNED;
			resource->flags &= ~IORESOURCE_STORED;
			base += size;
			
			printk(BIOS_SPEW, 
				"%s %02x *  [0x%08Lx - 0x%08Lx] %s\n",
				dev_path(dev),
				resource->index, 
				resource->base, 
				resource->base + resource->size - 1,
				(resource->flags & IORESOURCE_IO)? "io":
				(resource->flags & IORESOURCE_PREFETCH)? "prefmem": "mem");
		}
	}
	/* A pci bridge resource does not need to be a power
	 * of two size, but it does have a minimum granularity.
	 * Round the size up to that minimum granularity so we
	 * know not to place something else at an address postitively
	 * decoded by the bridge.
	 */
	bridge->size = round(base, bridge->gran) - bridge->base;

	printk(BIOS_SPEW, "%s compute_allocate_%s: base: %08Lx size: %08Lx align: %d gran: %d done\n", 
		dev_path(bus->dev),
		(bridge->flags & IORESOURCE_IO)? "io":
		(bridge->flags & IORESOURCE_PREFETCH)? "prefmem" : "mem",
		base, bridge->size, bridge->align, bridge->gran);


}

#if defined(CONFIG_CONSOLE_VGA) && CONFIG_CONSOLE_VGA == 1
struct device * vga_pri = 0;
static void allocate_vga_resource(void)
{
#warning "FIXME modify allocate_vga_resource so it is less pci centric!"
#warning "This function knows to much about PCI stuff, it should be just a ietrator/visitor."

	/* FIXME handle the VGA pallette snooping */
	struct device *dev, *vga, *vga_onboard, *vga_first, *vga_last;
	struct bus *bus;
	bus = 0;
	vga = 0;
	vga_onboard = 0;
	vga_first = 0;
	vga_last = 0;
	for(dev = all_devices; dev; dev = dev->next) {
		if (!dev->enabled) continue;
		if (((dev->class >> 16) == PCI_BASE_CLASS_DISPLAY) &&
			((dev->class >> 8) != PCI_CLASS_DISPLAY_OTHER)) 
		{
                        if (!vga_first) {
                                if (dev->on_mainboard) {
                                        vga_onboard = dev;
                                } else {
                                        vga_first = dev;
                                }
                        } else {
                                if (dev->on_mainboard) {
                                        vga_onboard = dev;
                                } else {
                                        vga_last = dev;
                                }
                        }

			/* It isn't safe to enable other VGA cards */
			dev->command &= ~(PCI_COMMAND_MEMORY | PCI_COMMAND_IO);
		}
	}
	
        vga = vga_last;

        if(!vga) {
                vga = vga_first;
        }

#if CONFIG_CONSOLE_VGA_ONBOARD_AT_FIRST == 1
        if (vga_onboard) // will use on board vga as pri
#else
        if (!vga) // will use last add on adapter as pri
#endif
        {
                vga = vga_onboard;
        }

	
	if (vga) {
		/* vga is first add on card or the only onboard vga */
		printk(BIOS_DEBUG, "Allocating VGA resource %s\n", dev_path(vga));
		/* All legacy VGA cards have MEM & I/O space registers */
		vga->command |= (PCI_COMMAND_MEMORY | PCI_COMMAND_IO);
		vga_pri = vga;
		bus = vga->bus;
	}
	/* Now walk up the bridges setting the VGA enable */
	while(bus) {
		printk(BIOS_DEBUG, "Setting PCI_BRIDGE_CTL_VGA for bridge %s\n",
			     dev_path(bus->dev));
		bus->bridge_ctrl |= PCI_BRIDGE_CTL_VGA;
		bus = (bus == bus->dev->bus)? 0 : bus->dev->bus;
	} 
}

#endif


/**
 * @brief  Assign the computed resources to the devices on the bus.
 *
 * @param bus Pointer to the structure for this bus
 *
 * Use the device specific set_resources method to store the computed
 * resources to hardware. For bridge devices, the set_resources() method
 * has to recurse into every down stream buses.
 *
 * Mutual recursion:
 *	assign_resources() -> device_operation::set_resources()
 *	device_operation::set_resources() -> assign_resources()
 */
void phase4_assign_resources(struct bus *bus)
{
	struct device *curdev;

	printk(BIOS_SPEW, "%s(%s) assign_resources, bus %d link: %d\n", 
		bus->dev->dtsname, dev_path(bus->dev), bus->secondary, bus->link);

	for(curdev = bus->children; curdev; curdev = curdev->sibling) {
		if (!curdev->enabled || !curdev->resources) {
			continue;
		}
		if (!curdev->ops) {
			printk(BIOS_ERR, "%s(%s) missing ops\n",
				curdev->dtsname, dev_path(curdev));
			continue;
		}
		if (!curdev->ops->phase4_set_resources) {
			printk(BIOS_ERR, "%s(%s) ops has no missing phase4_set_resources\n",
				curdev->dtsname, dev_path(curdev));
			continue;
		}
		curdev->ops->phase4_set_resources(curdev);
	}
	printk(BIOS_SPEW, "%s(%s) assign_resources, bus %d link: %d\n", 
		bus->dev->dtsname, dev_path(bus->dev), bus->secondary, bus->link);
}

/**
 * @brief Enable the resources for a specific device
 *
 * @param dev the device whose resources are to be enabled
 *
 * Enable resources of the device by calling the device specific
 * phase5() method.
 *
 * The parent's resources should be enabled first to avoid having enabling
 * order problem. This is done by calling the parent's phase5()
 * method and let that method to call it's children's phase5()
 * method via the (global) phase5_children().
 *
 * Indirect mutual recursion:
 *	dev_phase5() -> device_operations::phase5()
 *	device_operations::phase5() -> phase5_children()
 *	phase5_children() -> dev_phase5()
 */
void dev_phase5(struct device *dev)
{
	if (!dev->enabled) {
		return;
	}
	if (!dev->ops) {
		printk(BIOS_ERR, "%s: %s(%s) missing ops\n", __FUNCTION__, dev->dtsname, dev_path(dev));
		return;
	}
	if (!dev->ops->phase5_enable_resources) {
		printk(BIOS_ERR, "%s: %s(%s) ops are missing phase5_enable_resources\n", __FUNCTION__, dev->dtsname, dev_path(dev));
		return;
	}
	dev->ops->phase5_enable_resources(dev);
}

/** 
 * @brief Reset all of the devices a bus
 *
 * Reset all of the devices on a bus and clear the bus's reset_needed flag.
 *
 * @param bus pointer to the bus structure
 *
 * @return 1 if the bus was successfully reset, 0 otherwise.
 *
 */
int reset_bus(struct bus *bus)
{
	if (bus && bus->dev && bus->dev->ops && bus->dev->ops->reset_bus)
	{
		bus->dev->ops->reset_bus(bus);
		bus->reset_needed = 0;
		return 1;
	}
	return 0;
}

/**
 * @brief Do very early setup for all devices in the global device list.
 *
 * Starting at the first device on the global device link list,
 * walk the list and call the device's phase1() method to do very
 * early setup. phase1 should only used for devices that CAN NOT use printk, 
 * or that are part of making printk work. 
 */
void dev_phase1(void)
{
	struct device *dev;

	post_code(0x31);
	for(dev = all_devices; dev; dev = dev->next) {
		if (dev->ops && dev->ops->phase1) 
		{
			dev->ops->phase1(dev);
		}
	}
	post_code(0x3e);
	/* printk should be working at this point ... */
	printk(BIOS_INFO, "Phase 1: done\n");
	post_code(0x3f);
}

/**
 * @brief Do early setup for all devices in the global device list.
 *
 * Starting at the first device on the global device link list,
 * walk the list and call the device's phase2() method to do
 * early setup. You can use printk() in phase 2 methods. 
 */
void dev_phase2(void)
{
	struct device *dev;

	post_code(0x41);
	printk(BIOS_INFO, "Phase 2: Early setup...\n");
	for(dev = all_devices; dev; dev = dev->next) {
		printk(BIOS_SPEW, "%s: dev %s: ", __FUNCTION__, dev->dtsname);
		if (dev->ops && dev->ops->phase1) {
			printk(BIOS_SPEW, "Calling phase2 ..");
			dev->ops->phase2(dev);
			printk(BIOS_SPEW, " DONE");
		}
		printk(BIOS_SPEW, "\n");
	}
	post_code(0x4e);
	printk(BIOS_INFO, "Phase 2: Done.\n");
	post_code(0x4f);
}


/** 
 * @brief Scan for devices on a bus.
 *
 * If there are bridges on the bus, recursively scan the buses behind the bridges.
 * If the setting up and tuning of the bus causes a reset to be required, 
 * reset the bus and scan it again.
 *
 * @param bus pointer to the bus device
 * @param max current bus number
 *
 * @return The maximum bus number found, after scanning all subordinate busses
 */
unsigned int dev_phase3_scan(struct device * busdevice, unsigned int max)
{
	unsigned int new_max;
	int do_phase3;
	post_code(0x42);
	if (	!busdevice ||
		!busdevice->enabled ||
		!busdevice->ops ||
		!busdevice->ops->phase3_scan)
	{
		printk(BIOS_INFO, "%s: %s: busdevice %p enabled %d ops %p\n" , __FUNCTION__,
			busdevice->dtsname,
			busdevice, busdevice ? busdevice->enabled : NULL, 
			busdevice ? busdevice->ops : NULL);
		printk(BIOS_INFO, "%s: can not scan from here, returning %d\n", __FUNCTION__, max);
		return max;
	}
	do_phase3 = 1;
	while(do_phase3) {
		int link;
		printk(BIOS_INFO, "%s: scanning %s(%s)\n", __FUNCTION__, busdevice->dtsname, dev_path(busdevice));
		new_max = busdevice->ops->phase3_scan(busdevice, max);
		do_phase3 = 0;
		for(link = 0; link < busdevice->links; link++) {
			if (busdevice->link[link].reset_needed) {
				if (reset_bus(&busdevice->link[link])) {
					do_phase3 = 1;
				} else {
					busdevice->bus->reset_needed = 1;
				}
			}
		}
	}
	post_code(0x4e);
	printk(BIOS_INFO, "%s: returning %d\n", __FUNCTION__, max);
	return new_max;
}


/**
 * @brief Determine the existence of devices and extend the device tree.
 *
 * Most of the devices in the system are listed in the mainboard Config.lb
 * file. The device structures for these devices are generated at compile
 * time by the config tool and are organized into the device tree. This
 * function determines if the devices created at compile time actually exist
 * in the physical system.
 *
 * For devices in the physical system but not listed in the Config.lb file,
 * the device structures have to be created at run time and attached to the
 * device tree.
 *
 * This function starts from the root device 'dev_root', scan the buses in
 * the system recursively, modify the device tree according to the result of
 * the probe.
 *
 * This function has no idea how to scan and probe buses and devices at all.
 * It depends on the bus/device specific scan_bus() method to do it. The
 * scan_bus() method also has to create the device structure and attach
 * it to the device tree. 
 */
void dev_root_phase3(void)
{
	struct device *root;
	unsigned subordinate;
	printk(BIOS_INFO, "Phase 3: Enumerating buses...\n");
	root = &dev_root;
	if (root->chip_ops && root->chip_ops->enable_dev) {
		root->chip_ops->enable_dev(root);
	}
	post_code(0x41);
	if (!root->ops) {
		printk(BIOS_ERR, "dev_root_phase3 missing 'ops' initialization\nPhase 3: Failed.\n");
		return;
	}
	if (!root->ops->phase3_scan) {
		printk(BIOS_ERR, "dev_root ops struct missing 'phase3' initialization in ops structure\nPhase 3: Failed.");
		return;
	}
	subordinate = dev_phase3_scan(root, 0);
	printk(BIOS_INFO, "Phase 3: Done.\n");
}

/**
 * @brief Configure devices on the devices tree.
 * 
 * Starting at the root of the device tree, travel it recursively in two
 * passes. In the first pass, we compute and allocate resources (ranges)
 * requried by each device. In the second pass, the resources ranges are
 * relocated to their final position and stored to the hardware.
 *
 * I/O resources start at DEVICE_IO_START and grow upward. MEM resources start
 * at DEVICE_MEM_START and grow downward.
 *
 * Since the assignment is hierarchical we set the values into the dev_root
 * struct. 
 */
void dev_phase4(void)
{
	struct resource *io, *mem;
	struct device *root;

	printk(BIOS_INFO, "Phase 4: Allocating resources...\n");

	root = &dev_root;
	if (!root->ops) {
		printk(BIOS_ERR, "Phase 4: dev_root missing ops initialization\nPhase 4: Failed.\n");
		return;
	}	
	if (!root->ops->phase4_read_resources) {
		printk(BIOS_ERR, "dev_root ops missing read_resources\nPhase 4: Failed.\n");
		return;
	}

	if (!root->ops->phase4_set_resources) {
		printk(BIOS_ERR, "dev_root ops missing set_resources\nPhase 4: Failed.\n");
		return;
	}

	printk(BIOS_INFO, "Phase 4: Reading resources...\n");
	root->ops->phase4_read_resources(root);
	printk(BIOS_INFO, "Phase 4: Done reading resources.\n");

	/* Get the resources */
	io  = &root->resource[0];
	mem = &root->resource[1];
	/* Make certain the io devices are allocated somewhere safe. */
	io->base = DEVICE_IO_START;
	io->flags |= IORESOURCE_ASSIGNED;
	io->flags &= ~IORESOURCE_STORED;
	/* Now reallocate the pci resources memory with the
	 * highest addresses I can manage.
	 */
	mem->base = resource_max(&root->resource[1]);
	mem->flags |= IORESOURCE_ASSIGNED;
	mem->flags &= ~IORESOURCE_STORED;

#if defined(CONFIG_CONSOLE_VGA) && CONFIG_CONSOLE_VGA == 1
	/* Allocate the VGA I/O resource.. */
	allocate_vga_resource(); 
#endif

	/* Store the computed resource allocations into device registers ... */
	printk(BIOS_INFO, "Phase 4: Setting resources...\n");
	root->ops->phase4_set_resources(root);
	printk(BIOS_INFO, "Phase 4: Done setting resources.\n");
#if 0
	mem->flags |= IORESOURCE_STORED;
	report_resource_stored(root, mem, "");
#endif

	printk(BIOS_INFO, "Phase 4: Done allocating resources.\n");
}

/**
 * @brief Enable devices on the device tree.
 *
 * Starting at the root, walk the tree and enable all devices/bridges by
 * calling the device's enable_resources() method.
 */
void dev_root_phase5(void)
{
	printk(BIOS_INFO, "Phase 5: Enabling resources...\n");

	/* now enable everything. */
	dev_phase5(&dev_root);

	printk(BIOS_INFO, "Phase 5: Done.\n");
}

/**
 * @brief Initialize all devices in the global device list.
 *
 * Starting at the first device on the global device link list,
 * walk the list and call the device's init() method to do deivce
 * specific setup.
 */
void dev_phase6(void)
{
	struct device *dev;

	printk(BIOS_INFO, "Phase 6: Initializing devices...\n");
	for(dev = all_devices; dev; dev = dev->next) {
		if (dev->enabled && !dev->initialized && 
			dev->ops && dev->ops->phase6_init) 
		{
			if (dev->path.type == DEVICE_PATH_I2C) {
 				printk(BIOS_DEBUG, "Phase 6: smbus: %s[%d]->",
					dev_path(dev->bus->dev), dev->bus->link);
			}
			printk(BIOS_DEBUG, "Phase 6: %s init.\n", dev_path(dev));
			dev->initialized = 1;
			dev->ops->phase6_init(dev);
		}
	}
	printk(BIOS_INFO, "Phase 6: Devices initialized.\n");
}


void show_all_devs(void) {

        struct device *dev;

        printk(BIOS_INFO, "show all devs..\n");
        for(dev = all_devices; dev; dev = dev->next) {
		printk(BIOS_SPEW, "%s(%s): enabled %d have_resources %d initialized %d\n", 
			dev->dtsname, dev_path(dev), dev->enabled, dev->have_resources, dev->initialized);
        }
}

