// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2013 Intel Corporation; author Matt Fleming
 * Copyright (C) 2022 Markuss Broks <markuss.broks@gmail.com>
 */

#include <asm/early_ioremap.h>
#include <linux/console.h>
#include <linux/efi.h>
#include <linux/font.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/serial_core.h>
#include <linux/screen_info.h>

struct fb_earlycon {
	u32 x, y, curr_x, curr_y, depth, stride;
	size_t size;
	phys_addr_t phys_base;
	void __iomem *virt_base;
};

static const struct console *earlycon_console __initconst;
static struct fb_earlycon info;
static const struct font_desc *font;

static int __init simplefb_earlycon_remap_fb(void)
{
	unsigned long mapping;
	/* bail if there is no bootconsole or it has been disabled already */
	if (!earlycon_console || !(earlycon_console->flags & CON_ENABLED))
		return 0;

	if (region_intersects(info.phys_base, info.size,
			      IORESOURCE_SYSTEM_RAM, IORES_DESC_NONE) == REGION_INTERSECTS)		
		mapping = MEMREMAP_WB;
	else
		mapping = MEMREMAP_WC;

	info.virt_base = memremap(info.phys_base, info.size, mapping);

	return info.virt_base ? 0 : -ENOMEM;
}
early_initcall(simplefb_earlycon_remap_fb);

static int __init simplefb_earlycon_unmap_fb(void)
{
	/* unmap the bootconsole fb unless keep_bootcon has left it enabled */
	if (info.virt_base && !(earlycon_console->flags & CON_ENABLED))
		memunmap(info.virt_base);
	return 0;
}
late_initcall(simplefb_earlycon_unmap_fb);

static __ref void *simplefb_earlycon_map(unsigned long start, unsigned long len)
{
	pgprot_t fb_prot;

	if (info.virt_base)
		return info.virt_base + start;

	fb_prot = PAGE_KERNEL;
	return early_memremap_prot(info.phys_base + start, len, pgprot_val(fb_prot));
}

static __ref void simplefb_earlycon_unmap(void *addr, unsigned long len)
{
	if (info.virt_base)
		return;

	early_memunmap(addr, len);
}

static void simplefb_earlycon_clear_scanline(unsigned int y)
{
	unsigned long *dst;
	u16 len;

	len = info.stride;
	dst = simplefb_earlycon_map(y * len, len);
	if (!dst)
		return;

	memset(dst, 0, len);
	simplefb_earlycon_unmap(dst, len);
}

static void simplefb_earlycon_scroll_up(void)
{
	unsigned long *dst, *src;
	u16 len;
	u32 i, height;

	len = info.stride;
	height = info.y;

	for (i = 0; i < height - font->height; i++) {
		dst = simplefb_earlycon_map(i * len, len);
		if (!dst)
			return;

		src = simplefb_earlycon_map((i + font->height) * len, len);
		if (!src) {
			simplefb_earlycon_unmap(dst, len);
			return;
		}

		memmove(dst, src, len);

		simplefb_earlycon_unmap(src, len);
		simplefb_earlycon_unmap(dst, len);
	}
}

static void simplefb_earlycon_write_char(u8 *dst, unsigned char c, unsigned int h)
{
	const u8 *src;
	int m, n, bytes;
	u8 x;

	bytes = BITS_TO_BYTES(font->width);
	src = font->data + c * font->height * bytes + h * bytes;

	for (m = 0; m < font->width; m++) {
		n = m % 8;
		x = *(src + m / 8);
		if ((x >> (7 - n)) & 1)
			memset(dst, 0xff, (info.depth / 8));
		else
			memset(dst, 0, (info.depth / 8));
		dst += (info.depth / 8);
	}
}

static void
simplefb_earlycon_write(struct console *con, const char *str, unsigned int num)
{
	unsigned int len;
	const char *s;
	void *dst;

	len = info.stride;

	while (num) {
		unsigned int linemax;
		unsigned int h, count = 0;

		for (s = str; *s && *s != '\n'; s++) {
			if (count == num)
				break;
			count++;
		}

		linemax = (info.x - info.curr_x) / font->width;
		if (count > linemax)
			count = linemax;

		for (h = 0; h < font->height; h++) {
			unsigned int n, x;

			dst = simplefb_earlycon_map((info.curr_y + h) * len, len);
			if (!dst)
				return;

			s = str;
			n = count;
			x = info.curr_x;

			while (n-- > 0) {
				simplefb_earlycon_write_char(dst + (x * 4), *s, h);
				x += font->width;
				s++;
			}

			simplefb_earlycon_unmap(dst, len);
		}

		num -= count;
		info.curr_x += count * font->width;
		str += count;

		if (num > 0 && *s == '\n') {
			info.curr_x = 0;
			info.curr_y += font->height;
			str++;
			num--;
		}

		if (info.curr_x + font->width > info.x) {
			info.curr_x = 0;
			info.curr_y += font->height;
		}

		if (info.curr_y + font->height > info.y) {
			u32 i;

			info.curr_y -= font->height;
			simplefb_earlycon_scroll_up();

			for (i = 0; i < font->height; i++)
				simplefb_earlycon_clear_scanline(info.curr_y + i);
		}
	}
}

static int __init simplefb_earlycon_setup_common(struct earlycon_device *device,
						 const char *opt)
{
	int i;

	info.size = info.x * info.y * (info.depth / 8);

	font = get_default_font(info.x, info.y, -1, -1);
	if (!font)
		return -ENODEV;

	info.curr_y = rounddown(info.y, font->height) - font->height;
	for (i = 0; i < (info.y - info.curr_y) / font->height; i++)
		simplefb_earlycon_scroll_up();

	device->con->write = simplefb_earlycon_write;
	earlycon_console = device->con;
	return 0;
}

static int __init simplefb_earlycon_setup(struct earlycon_device *device,
					  const char *opt)
{
	struct uart_port *port = &device->port;
	int ret;

	if (!port->mapbase)
		return -ENODEV;

	info.phys_base = port->mapbase;

	ret = sscanf(device->options, "%ux%ux%u", &info.x, &info.y, &info.depth);
	if (ret != 3)
		return -ENODEV;

	info.stride = info.x * (info.depth / 8);

	return simplefb_earlycon_setup_common(device, opt);
}

EARLYCON_DECLARE(simplefb, simplefb_earlycon_setup);

#ifdef CONFIG_EFI_EARLYCON
static int __init simplefb_earlycon_setup_efi(struct earlycon_device *device,
					      const char *opt)
{
	if (screen_info.orig_video_isVGA != VIDEO_TYPE_EFI)
		return -ENODEV;

	info.phys_base = screen_info.lfb_base;
	if (screen_info.capabilities & VIDEO_CAPABILITY_64BIT_BASE)
		info.phys_base |= (u64)screen_info.ext_lfb_base << 32;

	info.x = screen_info.lfb_width;
	info.y = screen_info.lfb_height;
	info.depth = screen_info.lfb_depth;
	info.stride = screen_info.lfb_linelength;

	return simplefb_earlycon_setup_common(device, opt);
}

EARLYCON_DECLARE(efifb, simplefb_earlycon_setup_efi);
#endif

#ifdef CONFIG_OF_EARLY_FLATTREE
static int __init simplefb_earlycon_setup_of(struct earlycon_device *device,
					     const char *opt)
{
	struct uart_port *port = &device->port;
	const __be32 *val;

	if (!port->mapbase)
		return -ENODEV;

	info.phys_base = port->mapbase;

	val = of_get_flat_dt_prop(device->offset, "width", NULL);
	if (!val)
		return -ENODEV;
	info.x = be32_to_cpu(*val);

	val = of_get_flat_dt_prop(device->offset, "height", NULL);
	if (!val)
		return -ENODEV;
	info.y = be32_to_cpu(*val);

	val = of_get_flat_dt_prop(device->offset, "stride", NULL);
	if (!val)
		return -ENODEV;
	info.stride = be32_to_cpu(*val);
	info.depth = (info.stride / info.x) * 8;

	return simplefb_earlycon_setup_common(device, opt);
}

OF_EARLYCON_DECLARE(simplefb, "simple-framebuffer", simplefb_earlycon_setup_of);
#endif
