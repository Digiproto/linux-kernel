/*
 *  Copyright (C) 2013, Loongson Technology Corporation Limited, Inc.
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 */
#include <linux/init.h>
#include <asm/io.h>
#include <south-bridge.h>
#include <boot_param.h>
#include <dma-coherence.h>
#include <ls2h/ls2h.h>

extern void ls2h_init_irq(void);
extern void ls2h_irq_dispatch(unsigned int pending);
extern int ls2h_platform_init(void);

#ifdef CONFIG_LS2H_PCIE

#ifdef CONFIG_SWIOTLB
extern void ls2h_init_swiotlb(void);
#endif
extern void ls2h_pcie_init(void);
extern int ls2h_pcie_map_irq(struct pci_dev *dev, u8 slot, u8 pin);
extern int ls2h_init_pcie_bios(struct pci_dev *pdev);
#endif /* CONFIG_LS2H_PCIE */

static dma_addr_t ls2h_unity_phys_to_dma(struct device *dev, phys_addr_t paddr)
{
	return paddr & 0xffffffff;
}

static phys_addr_t ls2h_unity_dma_to_phys(struct device *dev, dma_addr_t daddr)
{
	return (daddr < 0x10000000) ? daddr : (daddr | 0x100000000);
}

static void ls2h_dma_ops_init(void)
{
	loongson_dma_map_ops->dma_to_phys = ls2h_unity_dma_to_phys;
	loongson_dma_map_ops->phys_to_dma = ls2h_unity_phys_to_dma;
}

static void enable_south_bridge(void)
{
	u32 i;

	/*
	 * Loongson2H chip_config0
	 * bit[5]: loongson2h bridge model enable. 1:enable  0:disable
	 */
	i = ls2h_readl(LS2H_CHIP_CFG0_REG) | (1 << 5);
	ls2h_writel(i, LS2H_CHIP_CFG0_REG);
}

static void __init ls2h_arch_initcall(void)
{
	enable_south_bridge();

	ls2h_dma_ops_init();

#ifdef CONFIG_LS2H_PCIE
	ls2h_pcie_init();
#endif
}

static void __init ls2h_device_initcall(void)
{
	ls2h_platform_init();
}

const struct south_bridge ls2h_south_bridge = {
	.sb_init_irq		= ls2h_init_irq,
	.sb_irq_dispatch	= ls2h_irq_dispatch,
#ifdef CONFIG_LS2H_PCIE

#ifdef CONFIG_SWIOTLB
	.sb_init_swiotlb	= ls2h_init_swiotlb,
#endif
	.sb_pcibios_init	= ls2h_init_pcie_bios,
	.sb_pcibios_map_irq	= ls2h_pcie_map_irq,
#endif /* CONFIG_LS2H_PCIE */
	.sb_arch_initcall	= ls2h_arch_initcall,
	.sb_device_initcall	= ls2h_device_initcall,
};
