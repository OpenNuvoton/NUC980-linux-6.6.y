/*
 * Copyright (c) 2026 Nuvoton Technology Corp.
 *
 * NUC980 Series PWM driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
*/


#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/pwm.h>
#include <linux/pinctrl/consumer.h>
#include <mach/map.h>
//#include <mach/regs-pwm1.h>
#include <mach/regs-clock.h>

//#define DEBUG_PWM

#define REG_PWM_PPR             (0x00)
#define REG_PWM_CSR             (0x04)
#define REG_PWM_PCR             (0x08)
#define REG_PWM_CNR0            (0x0C)
#define REG_PWM_CMR0            (0x10)
#define REG_PWM_PDR0            (0x14)
#define REG_PWM_CNR1            (0x18)
#define REG_PWM_CMR1            (0x1C)
#define REG_PWM_PDR1            (0x20)
#define REG_PWM_CNR2            (0x24)
#define REG_PWM_CMR2            (0x28)
#define REG_PWM_PDR2            (0x2C)
#define REG_PWM_CNR3            (0x30)
#define REG_PWM_CMR3            (0x34)
#define REG_PWM_PDR3            (0x38)
#define REG_PWM_PIER            (0x3C)
#define REG_PWM_PIIR            (0x40)

#define NUC980_PWM_MAX_COUNT           0xFFFF
#define NUC980_PWM_TOTAL_CHANNELS      4

struct nuc980_chip {
	struct platform_device	*pdev;
	struct clk		*clk;
	struct pwm_chip		 chip;
	void __iomem		*regs;
	spinlock_t		lock;
	u64			clkrate;
};

#define to_nuc980_chip(chip)	container_of(chip, struct nuc980_chip, chip)

#ifdef DEBUG_PWM
static void pwm_dbg(struct pwm_chip *chip)
{
	struct nuc980_chip *nuc980 = to_nuc980_chip(chip);

	printk("PPR: 0x%08x\n", readl(nuc980->regs + REG_PWM_PPR));
	printk("CSR: 0x%08x\n", readl(nuc980->regs + REG_PWM_CSR));
	printk("PCR: 0x%08x\n", readl(nuc980->regs + REG_PWM_PCR));
	printk("CNR0:0x%08x\n", readl(nuc980->regs + REG_PWM_CNR0));
	printk("CMR0:0x%08x\n", readl(nuc980->regs + REG_PWM_CMR0));
	printk("CNR1:0x%08x\n", readl(nuc980->regs + REG_PWM_CNR1));
	printk("CMR1:0x%08x\n", readl(nuc980->regs + REG_PWM_CMR1));
	printk("CNR2:0x%08x\n", readl(nuc980->regs + REG_PWM_CNR2));
	printk("CMR2:0x%08x\n", readl(nuc980->regs + REG_PWM_CMR2));
	printk("CNR3:0x%08x\n", readl(nuc980->regs + REG_PWM_CNR3));
	printk("CMR3:0x%08x\n", readl(nuc980->regs + REG_PWM_CMR3));
	printk("PIER:0x%08x\n", readl(nuc980->regs + REG_PWM_PIER));
	printk("PIIR:0x%08x\n", readl(nuc980->regs + REG_PWM_PIIR));

}
#endif

static int nuvoton_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
                             const struct pwm_state *state)
{
	struct nuc980_chip *nuc980 = to_nuc980_chip(chip);
	unsigned long period, duty, prescale;
	int ch = pwm->hwpwm;

#ifdef DEBUG_PWM
	printk("Enter %s ....ch[%d]\n",__FUNCTION__, pwm->hwpwm);
#endif

	if (state->enabled) {

		// Get PCLK, calculate valid parameter range.
		prescale = clk_get_rate(nuc980->clk) / 1000000 - 1;

		// now pwm time unit is 1000ns.
		//period = (state->period + 500) / 1000;
		period = (unsigned long)div_u64(state->period + 500, 1000);
		//duty = (state->duty_cycle + 500) / 1000;
		duty = (unsigned long)div_u64(state->duty_cycle + 500, 1000);

		// don't want the minus 1 below change the value to -1 (0xFFFF)
		if (period == 0)
			period = 1;
		if (duty == 0)
			duty = 1;

		// Set prescale for all pwm channels
		writel(prescale | (prescale << 8), nuc980->regs + REG_PWM_PPR);

		if(ch == 0) {
			writel(readl(nuc980->regs + REG_PWM_PCR) | (9), nuc980->regs + REG_PWM_PCR);
			writel(period - 1, nuc980->regs + REG_PWM_CNR0);
			writel(duty - 1, nuc980->regs + REG_PWM_CMR0);
			if (state->polarity == PWM_POLARITY_NORMAL)
				writel(readl(nuc980->regs + REG_PWM_PCR) & ~(4), nuc980->regs + REG_PWM_PCR);
			else
				writel(readl(nuc980->regs + REG_PWM_PCR) | (4), nuc980->regs + REG_PWM_PCR);
		} else if(ch == 1) {
			writel(readl(nuc980->regs + REG_PWM_PCR) | (9 << 8), nuc980->regs + REG_PWM_PCR);
			writel(period - 1, nuc980->regs + REG_PWM_CNR1);
			writel(duty - 1, nuc980->regs + REG_PWM_CMR1);
			if (state->polarity == PWM_POLARITY_NORMAL)
				writel(readl(nuc980->regs + REG_PWM_PCR) & ~(4 << 8), nuc980->regs + REG_PWM_PCR);
			else
				writel(readl(nuc980->regs + REG_PWM_PCR) | (4 << 8), nuc980->regs + REG_PWM_PCR);
		} else if (ch == 2) {
			writel(readl(nuc980->regs + REG_PWM_PCR) | (9 << 12), nuc980->regs + REG_PWM_PCR);
			writel(period - 1, nuc980->regs + REG_PWM_CNR2);
			writel(duty - 1, nuc980->regs + REG_PWM_CMR2);
			if (state->polarity == PWM_POLARITY_NORMAL)
				writel(readl(nuc980->regs + REG_PWM_PCR) & ~(4 << 12), nuc980->regs + REG_PWM_PCR);
			else
				writel(readl(nuc980->regs + REG_PWM_PCR) | (4 << 12), nuc980->regs + REG_PWM_PCR);
		} else {/* ch 3 */
			writel(readl(nuc980->regs + REG_PWM_PCR) | (9 << 16), nuc980->regs + REG_PWM_PCR);
			writel(period - 1, nuc980->regs + REG_PWM_CNR3);
			writel(duty - 1, nuc980->regs + REG_PWM_CMR3);
			if (state->polarity == PWM_POLARITY_NORMAL)
				writel(readl(nuc980->regs + REG_PWM_PCR) & ~(4 << 16), nuc980->regs + REG_PWM_PCR);
			else
				writel(readl(nuc980->regs + REG_PWM_PCR) | (4 << 16), nuc980->regs + REG_PWM_PCR);
		}
	} else {
		if (ch == 0)
			writel(readl(nuc980->regs + REG_PWM_PCR) & ~(1), nuc980->regs + REG_PWM_PCR);
		else if (ch == 1)
			writel(readl(nuc980->regs + REG_PWM_PCR) & ~(1 << 8), nuc980->regs + REG_PWM_PCR);
		else if (ch == 2)
			writel(readl(nuc980->regs + REG_PWM_PCR) & ~(1 << 12), nuc980->regs + REG_PWM_PCR);
		else    /* ch 3 */
			writel(readl(nuc980->regs + REG_PWM_PCR) & ~(1 << 16), nuc980->regs + REG_PWM_PCR);

	}

#ifdef DEBUG_PWM
	pwm_dbg(chip);
#endif

	return 0;
}

static int nuvoton_pwm_get_state(struct pwm_chip *chip, struct pwm_device *pwm,
                                 struct pwm_state *state)
{
	struct nuc980_chip *nuc980 = to_nuc980_chip(chip);
	u32 duty_cycles, period_cycles, cnten, outen, polarity, prescale;
	u32 ch = pwm->hwpwm;

#ifdef DEBUG_PWM
	printk("Enter %s ....ch[%d]\n",__FUNCTION__,pwm->hwpwm);
#endif


	if (ch == 0) {
		cnten = readl(nuc980->regs + REG_PWM_PCR) & 1;
		outen = readl(nuc980->regs + REG_PWM_PCR) & 1;
		duty_cycles = readl(nuc980->regs + REG_PWM_CMR0);
		period_cycles = readl(nuc980->regs + REG_PWM_CNR0);
		polarity = (readl(nuc980->regs + REG_PWM_PCR) >> 2) & 1;
	} else if (ch == 1) {
		cnten = (readl(nuc980->regs + REG_PWM_PCR) & 0x100) >> 8;
		outen = (readl(nuc980->regs + REG_PWM_PCR) & 0x100) >> 8;
		duty_cycles = readl(nuc980->regs + REG_PWM_CMR1);
		period_cycles = readl(nuc980->regs + REG_PWM_CNR1);
		polarity = (readl(nuc980->regs + REG_PWM_PCR) >> 10) & 1;
	} else if (ch == 2) {
		cnten = (readl(nuc980->regs + REG_PWM_PCR) & 0x100) >> 12;
		outen = (readl(nuc980->regs + REG_PWM_PCR) & 0x100) >> 12;
		duty_cycles = readl(nuc980->regs + REG_PWM_CMR2);
		period_cycles = readl(nuc980->regs + REG_PWM_CNR2);
		polarity = (readl(nuc980->regs + REG_PWM_PCR) >> 14) & 1;
	} else if (ch == 3) {
		cnten = (readl(nuc980->regs + REG_PWM_PCR) & 0x100) >> 16;
		outen = (readl(nuc980->regs + REG_PWM_PCR) & 0x100) >> 16;
		duty_cycles = readl(nuc980->regs + REG_PWM_CMR3);
		period_cycles = readl(nuc980->regs + REG_PWM_CNR3);
		polarity = (readl(nuc980->regs + REG_PWM_PCR) >> 18) & 1;
	}

	if ((ch == 0) || (ch == 1))
		prescale = (readl(nuc980->regs + REG_PWM_PPR) & 0xFF) + 1;
	else
		prescale = ((readl(nuc980->regs + REG_PWM_PPR) & 0xFF00) >> 8) + 1;

	state->enabled = cnten;
	state->polarity = polarity ? PWM_POLARITY_INVERSED : PWM_POLARITY_NORMAL;
	state->duty_cycle = DIV64_U64_ROUND_UP((u64)duty_cycles * NSEC_PER_SEC, nuc980->clkrate);
	state->period = DIV64_U64_ROUND_UP((u64)period_cycles * NSEC_PER_SEC, nuc980->clkrate);

#ifdef DEBUG_PWM
	pwm_dbg(chip);
#endif

	return 0;
}

static struct pwm_ops nuc980_pwm_ops = {
	.apply = nuvoton_pwm_apply,
	.get_state = nuvoton_pwm_get_state,
};

static int nuc980_pwm_probe(struct platform_device *pdev)
{

	struct nuc980_chip *nuc980;
	struct pinctrl *p;
	struct resource *r;
	int ret;
#if defined(CONFIG_USE_OF)
	u32 id;
#endif

	nuc980 = devm_kzalloc(&pdev->dev, sizeof(*nuc980), GFP_KERNEL);
	if (nuc980 == NULL) {
		dev_err(&pdev->dev, "failed to allocate memory for pwm_device\n");
		return -ENOMEM;
	}

	/* calculate base of control bits in TCON */

	nuc980->chip.dev = &pdev->dev;
	nuc980->chip.ops = &nuc980_pwm_ops;
	nuc980->chip.npwm = 4;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	nuc980->regs = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(nuc980->regs))
		return PTR_ERR(nuc980->regs);

	/*
	nuc980->clk = of_clk_get(pdev->dev.of_node, 0);
	if (IS_ERR(nuc980->clk)) {
		dev_err(&pdev->dev, "failed to get epwm clock\n");
		ret = PTR_ERR(nuc980->clk);
		return -ENOENT;
	}
	err = clk_prepare_enable(nuc980->clk);
	if (err)
		return -ENOENT;
		*/

	nuc980->clk = clk_get(NULL, "pwm1");
	if (IS_ERR(nuc980->clk)) {
		dev_err(&pdev->dev, "failed to get pwm clock\n");
		ret = PTR_ERR(nuc980->clk);
		return ret;
	}

	clk_prepare(nuc980->clk);
	clk_enable(nuc980->clk);

	nuc980->clkrate = clk_get_rate(nuc980->clk);

	// all channel prescale output div by 1
	writel(0x4444, nuc980->regs + REG_PWM_CSR);

#if defined(CONFIG_USE_OF)
	if (of_property_read_u32(pdev->dev.of_node, "id", &id)) {
		printk("can't get pwm id from dt\n");
	} else {
		pdev->id = id;
	}
#endif

	if(pdev->id == 0) {
#if defined(CONFIG_USE_OF)
		p = devm_pinctrl_get_select_default(&pdev->dev);
#else
#if defined (CONFIG_NUC980_PWM1_CH0_PB12)
		p = devm_pinctrl_get_select(&pdev->dev, "pwm10-PB12");
#elif defined (CONFIG_NUC980_PWM1_CH0_PG6)
		p = devm_pinctrl_get_select(&pdev->dev, "pwm10-PG6");
#elif defined (CONFIG_NUC980_PWM1_CH0_PG11)
		p = devm_pinctrl_get_select(&pdev->dev, "pwm10-PG11");
#elif defined (CONFIG_NUC980_PWM1_CH0_PF9)
		p = devm_pinctrl_get_select(&pdev->dev, "pwm10-PF9");
#endif

#ifndef CONFIG_NUC980_PWM1_CH0_NONE
		if(IS_ERR(p)) {
			dev_err(&pdev->dev, "unable to reserve output pin\n");

		}
#endif
#endif
	}
	if(pdev->id == 1) {
#if defined(CONFIG_USE_OF)
		p = devm_pinctrl_get_select_default(&pdev->dev);
#else
#if defined (CONFIG_NUC980_PWM1_CH1_PB11)
		p = devm_pinctrl_get_select(&pdev->dev, "pwm11-PB11");
#elif defined (CONFIG_NUC980_PWM1_CH1_PG7)
		p = devm_pinctrl_get_select(&pdev->dev, "pwm11-PG7");
#elif defined (CONFIG_NUC980_PWM1_CH1_PG12)
		p = devm_pinctrl_get_select(&pdev->dev, "pwm11-PG12");
#elif defined (CONFIG_NUC980_PWM1_CH1_PF10)
		p = devm_pinctrl_get_select(&pdev->dev, "pwm11-PF10");
#endif

#ifndef CONFIG_NUC980_PWM1_CH1_NONE
		if(IS_ERR(p)) {
			dev_err(&pdev->dev, "unable to reserve output pin\n");
		}
#endif
#endif
	}
	if(pdev->id == 2) {
#if defined(CONFIG_USE_OF)
		p = devm_pinctrl_get_select_default(&pdev->dev);
#else
#if defined (CONFIG_NUC980_PWM1_CH2_PB10)
		p = devm_pinctrl_get_select(&pdev->dev, "pwm12-PB10");
#elif defined (CONFIG_NUC980_PWM1_CH2_PG8)
		p = devm_pinctrl_get_select(&pdev->dev, "pwm12-PG8");
#elif defined (CONFIG_NUC980_PWM1_CH2_PG13)
		p = devm_pinctrl_get_select(&pdev->dev, "pwm12-PG13");
#elif defined (CONFIG_NUC980_PWM1_CH2_PE10)
		p = devm_pinctrl_get_select(&pdev->dev, "pwm12-PE10");
#endif

#ifndef CONFIG_NUC980_PWM1_CH2_NONE
		if(IS_ERR(p)) {
			dev_err(&pdev->dev, "unable to reserve output pin\n");
		}
#endif
#endif
	}
	if(pdev->id == 3) {
#if defined(CONFIG_USE_OF)
		p = devm_pinctrl_get_select_default(&pdev->dev);
#else
#if defined (CONFIG_NUC980_PWM1_CH3_PB9)
		p = devm_pinctrl_get_select(&pdev->dev, "pwm13-PB9");
#elif defined (CONFIG_NUC980_PWM1_CH3_PG9)
		p = devm_pinctrl_get_select(&pdev->dev, "pwm13-PG9");
#elif defined (CONFIG_NUC980_PWM1_CH3_PG14)
		p = devm_pinctrl_get_select(&pdev->dev, "pwm13-PG14");
#elif defined (CONFIG_NUC980_PWM1_CH3_PE12)
		p = devm_pinctrl_get_select(&pdev->dev, "pwm13-PE12");
#endif

#ifndef CONFIG_NUC980_PWM1_CH3_NONE
		if(IS_ERR(p)) {
			dev_err(&pdev->dev, "unable to reserve output pin\n");
		}
#endif
#endif
	}

	ret = pwmchip_add(&nuc980->chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to register pwm\n");
		goto err;
	}

	platform_set_drvdata(pdev, nuc980);

	return 0;

err:
	//clk_disable(nuc980->clk);
	return ret;
}

#if defined(CONFIG_USE_OF)
static const struct of_device_id nuc980_pwm1_of_match[] = {
	{   .compatible = "nuvoton,nuc980-pwm1" },
	{	},
};
MODULE_DEVICE_TABLE(of, nuc980_pwm1_of_match);
#endif

static struct platform_driver nuc980_pwm1_driver = {
	.driver		= {
		.name	= "nuc980-pwm1",
		.owner	= THIS_MODULE,
#if defined(CONFIG_USE_OF)
		.of_match_table = of_match_ptr(nuc980_pwm1_of_match),
#endif
//#ifdef CONFIG_PM
//		.pm	= &nuc980_pwm_pm_ops,
//#endif
	},
	.probe		= nuc980_pwm_probe,
};

module_platform_driver(nuc980_pwm1_driver);

MODULE_AUTHOR("Chi-Wen Weng <cwweng@nuvoton.com>");
MODULE_DESCRIPTION("Nuvoton NUC980 PWM1 driver");
MODULE_LICENSE("GPL");
